// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek SDIO host driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/utsname.h>
#include <soc/realtek/rtk_chip.h>

#include "sdhci-pltfm.h"
#include "sdhci-rtk.h"

#define DRIVER_NAME		"Realtek SDIO"
#define BANNER			"Realtek SDIO Host Driver"
#define SDIO_CLKEN_REGOFF	0x0C
#define SDIO_CLKEN_REGBIT	30

#define MAX_PHASE		31
#define TUNING_CNT		3

#define sdhci_tx_tuning

static void __iomem *sdio_membase;
static void __iomem *crt_membase;
static struct gpio_desc *sdio_gpio_23;
bool SDIO_module_disable;
bool clock_enable;
bool SDIO_card;
int SDIO_version;
struct sdhci_host *G_host;
unsigned int drive_temp;
static enum rtd_chip_id chip_id;

struct reset_control *rstc_sdio;
struct clk *clk_en_sdio;
struct clk *clk_en_sdio_ip;

struct sdhci_rtk_sdio_data {
	const struct sdhci_pltfm_data *pdata;
};

struct timing_phase_path {
	int start;
	int end;
	int mid;
	int len;
};

void set_SDIO_version(int version)
{
	SDIO_version = version;
}

int get_SDIO_version(void)
{
	return SDIO_version;
}

void disable_sdio_irq(struct sdhci_host *host)
{
	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
}
EXPORT_SYMBOL(disable_sdio_irq);

static void rtk_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		writel(0x00000003, crt_membase + 0x010A10);

	sdhci_reset(host, mask);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		writel(0x00000001, crt_membase + 0x010A10);
}

static void rtk_sdhci_pad_driving_configure(void)
{
	if (chip_id == CHIP_ID_RTD1619) {
		writel((readl(crt_membase + 0x4E024) & 0xf) | 0xAF75EEB0, crt_membase + 0x4E024);
		writel(0x5EEBDD7B, crt_membase + 0x4E028);
		writel((readl(crt_membase + 0x4E02c) & 0xffffffc0) | 0x37, crt_membase + 0x4E02c);
	} else if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319) {
		writel((readl(crt_membase + 0x4E058) & 0xff000000) | 0x00483483, crt_membase + 0x4E058);
		writel((readl(crt_membase + 0x4E05c) & 0xff000000) | 0x00483483, crt_membase + 0x4E05c);
		writel((readl(crt_membase + 0x4E060) & 0xff000000) | 0x00483483, crt_membase + 0x4E060);
	} else if (chip_id == CHIP_ID_RTD1619B) {
		writel((readl(crt_membase + 0x4E068) & 0xff80ffff) | 0x00240000, crt_membase + 0x4E068);
		writel((readl(crt_membase + 0x4E06c) & 0xfc07e03f) | 0x01200900, crt_membase + 0x4E06c);
		writel((readl(crt_membase + 0x4E070) & 0xfc07e03f) | 0x01200900, crt_membase + 0x4E070);
		writel((readl(crt_membase + 0x4E074) & 0xffffff80) | 0x00000024, crt_membase + 0x4E074);
	}
}

static void rtk_sdhci_pad_pwrctrl(int voltage)
{
	if (voltage == MMC_SIGNAL_VOLTAGE_180) {
		writel((readl(crt_membase + 0x4E068) & 0xffbfffff) , crt_membase + 0x4E068);
		writel((readl(crt_membase + 0x4E06c) & 0xfdffefff) , crt_membase + 0x4E06c);
		writel((readl(crt_membase + 0x4E070) & 0xfdffefff) , crt_membase + 0x4E070);
		writel((readl(crt_membase + 0x4E074) & 0xffffffbf) , crt_membase + 0x4E074);
	} else if (voltage == MMC_SIGNAL_VOLTAGE_330) {
		writel((readl(crt_membase + 0x4E068) | 0x00400000) , crt_membase + 0x4E068);
		writel((readl(crt_membase + 0x4E06c) | 0x02001000) , crt_membase + 0x4E06c);
		writel((readl(crt_membase + 0x4E070) | 0x02001000) , crt_membase + 0x4E070);
		writel((readl(crt_membase + 0x4E074) | 0x00000040) , crt_membase + 0x4E074);
	}
}

static void rtk_sdhci_pll_configure(u32 pll, int execute_tuning)
{
	u32 sscpll;

	writel(0x00000006, crt_membase + 0x01AC);
	if (chip_id == CHIP_ID_RTD1619B)
		sscpll = 0x0451742d;
	else
		sscpll = 0x04517893;
	/* The SSC shouldn't enable when execute tuning */
	if(!execute_tuning)
		writel(sscpll, crt_membase + 0x01A4);
	writel(pll, crt_membase + 0x01A8);
	mdelay(2);
	writel(0x00000007, crt_membase + 0x01AC);
	udelay(200);
}

static void rtk_sdhci_uhs_signaling(struct sdhci_host *host,
				    unsigned int timing)
{
	u16 ctrl_2 = 0;

	sdhci_set_uhs_signaling(host, timing);

	if (timing > MMC_TIMING_SD_HS) {
		ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		sdhci_writew(host, ctrl_2 | SDHCI_CTRL_VDD_180, SDHCI_HOST_CONTROL2);
	}
}

static void rtk_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;

	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);
	sdhci_enable_clk(host, clk);

	switch (host->timing) {

	case MMC_TIMING_SD_HS:
		set_SDIO_version(2);
		rtk_register_set();
		break;
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
		set_SDIO_version(3);
		rtk_register_set();
		break;
	case MMC_TIMING_UHS_SDR50:
		set_SDIO_version(3);
		rtk_register_set();
		sdhci_writew(host, 0x7, SDHCI_CLOCK_CONTROL);
		rtk_sdhci_pll_configure(0x00564388, 0);
		break;
	case MMC_TIMING_UHS_SDR104:
		set_SDIO_version(4);
		rtk_register_set();
		break;
	}

	pr_debug("[SDIO] SDHCI_SPEC_300: %s c3c=%x, PLL=%x, CLK=%x\n",
	       __func__, readl(host->ioaddr + 0x3c), readl(crt_membase + 0x01A8),
	       readl(host->ioaddr + SDHCI_CLOCK_CONTROL));

	if ((clk != 0xfa00) && (clk != 0x0) && (clk != 0x100) && (clk != 0x200)) {
		host->mmc->caps2 |=  MMC_CAP2_NO_SDIO;
		SDIO_card = false;
	}
}

void rtk_register_set(void)
{

	if (get_SDIO_version() == 3 || get_SDIO_version() == 4) {

		if (chip_id == CHIP_ID_RTD1619)
			writel((readl(crt_membase + 0x1A0) & ~0x000000F8) | (0x1d << 3), crt_membase + 0x1A0);

		rtk_sdhci_pad_driving_configure();

		if (chip_id != CHIP_ID_RTD1619B)
			writel(0x00000000, crt_membase + 0x010A40);

		if (get_SDIO_version() == 4) {
			/* enlarge wait time for data */
			if (chip_id == CHIP_ID_RTD1619)
				writel(0x1d, crt_membase + 0x10a58);

			rtk_sdhci_pll_configure(0x00b64388, 0);
		}
	} else if (get_SDIO_version() == 2) {
		if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319) {
			writel(readl(crt_membase + 0x7064) | (0x3 << 4), crt_membase + 0x7064);
		} else if (chip_id == CHIP_ID_RTD1619B) {
			writel(readl(crt_membase + 0x70c4) | (0x3 << 4), crt_membase + 0x70c4);
			rtk_sdhci_pad_pwrctrl(MMC_SIGNAL_VOLTAGE_330);
		} else {
			writel(0x00000003, crt_membase + 0x01A0);
		}
	}
}

void rtk_sdhci_platform_init(void)
{
#if defined(SD_INTERFACE_SDIO_1619)
	unsigned int tmp = 0;
#endif
	if (chip_id == CHIP_ID_RTD1619) {
		if (get_rtd_chip_revision() == RTD_CHIP_A01) {
			writel(readl(crt_membase + 0x1A0) | 0x1, crt_membase + 0x1A0);
			writel(readl(crt_membase + 0x1A4) | 0x1, crt_membase + 0x1A4);
			udelay(200);
		}
	} else if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319) {
		writel(readl(crt_membase + 0x7064) | (0x3 << 4), crt_membase + 0x7064);
		udelay(200);
	} else if (chip_id == CHIP_ID_RTD1619B) {
		writel(readl(crt_membase + 0x70c4) | (0x3 << 4), crt_membase + 0x70c4);
		udelay(200);
		/*  The VDD resistor set 1.8v as default */
		rtk_sdhci_pad_pwrctrl(MMC_SIGNAL_VOLTAGE_180);
	}

	rtk_sdhci_pll_configure(0x00ae4388, 0);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319) {
		if (get_rtd_chip_revision() == RTD_CHIP_A00){
			writel((readl(crt_membase + 0x1C200) & 0xfffff0ff), crt_membase + 0x1C200);
			writel(readl(crt_membase + 0x1C208) | (0x1 << 5), crt_membase + 0x1C208);
		} else {
			writel(readl(crt_membase + 0x10A58) | 0x80000000, crt_membase + 0x10A58);
		}
	}

	writel(0x00000011, crt_membase + 0x010A34);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		writel(0x00000001, crt_membase + 0x010A10);
	else
		writel(0x00000003, crt_membase + 0x010A10);

#if defined(SD_INTERFACE_SDIO_1619)
	writel(readl(crt_membase + 0x4E004) & (~(0x1 << 25)),
	       crt_membase + 0x4E004);
	writel((readl(crt_membase + 0x4E020) & 0xff0fffff) | 0x00600000,
	       crt_membase + 0x4E020);
	mdelay(100);
	writel(readl(crt_membase + 0x4E020) & 0xffbfffff,
	       crt_membase + 0x4E020);
	mdelay(100);

	tmp = (0x3243243 << 4) | (readl(crt_membase + 0x4f00c) & 0xf);
	writel(tmp, crt_membase + 0x4f00c);
	writel(0xd83d83d8, crt_membase + 0x4f010);
	tmp = (readl(crt_membase + 0x4f014) & 0xfffff000) | 0xd83;
	writel(tmp, crt_membase + 0x4f014);

	writel(0x10, crt_membase + 0x4E048);
	writel((readl(crt_membase + 0x4F004) & 0xfff00000) | 0x52492,
	       crt_membase + 0x4F004);
	writel(0x3, crt_membase + 0x1e0);
	writel(0x4003, crt_membase + 0x1e0);
	writel(0x6003, crt_membase + 0x1e0);
#elif defined(SD_INTERFACE_SDIO_1319)
	writel(readl(crt_membase + 0x4E00C) & (~(0x1 << 2)),
	       crt_membase + 0x4E00C);
	writel((readl(crt_membase + 0x4E048) & 0xfff0ffff) | 0x00060000,
	       crt_membase + 0x4E048);
	mdelay(100);
	writel(readl(crt_membase + 0x4E048) & 0xfffbffff,
	       crt_membase + 0x4E048);
	mdelay(100);

	writel((readl(crt_membase + 0x4E04C) & 0xff000000) | 0x243243,
	       crt_membase + 0x4E04C);
	writel((readl(crt_membase + 0x4E050) & 0xf000000f) | (0x243243 << 4),
	       crt_membase + 0x4E050);
	writel((readl(crt_membase + 0x4E054) & 0xff000000) | 0x243243,
	       crt_membase + 0x4E054);

	writel((readl(crt_membase + 0x4E010) & 0xfffffff0) | (0x1 << 1),
	       crt_membase + 0x4E010);
	writel(0x10, crt_membase + 0x4E120);
	writel((readl(crt_membase + 0x4E00C) & 0xf00f) | 0x04440480,
	       crt_membase + 0x4E00C);
	writel(0x3, crt_membase + 0x1e0);
	writel(0x4003, crt_membase + 0x1e0);
	writel(0x6003, crt_membase + 0x1e0);
#elif defined(SD_INTERFACE_SDIO_1619B)
	writel(readl(crt_membase + 0x7100) | (0x1 << 30), crt_membase + 0x7100);
	writel(readl(crt_membase + 0x7104) | (0x0 << 30), crt_membase + 0x7104);
	writel((readl(crt_membase + 0x4E080) & 0xfffFBfff) | 0x00004000,
	       crt_membase + 0x4E080);
	mdelay(10);

	writel((readl(crt_membase + 0x4E078) & 0xfe07f03f) | 0x00d806c0, crt_membase + 0x4E078);
	writel((readl(crt_membase + 0x4E07c) & 0xfff81fc0) | 0x0003601b, crt_membase + 0x4E07c);
	writel((readl(crt_membase + 0x4E080) & 0xfe07f03f) | 0x00d806c0, crt_membase + 0x4E080);

	writel((readl(crt_membase + 0x4E00c) & 0xff801fff) | 0x00084000,
	       crt_membase + 0x4E00c);
	writel((readl(crt_membase + 0x4E010) & 0xfff80000) | 0x00008842,
	       crt_membase + 0x4E010);
	writel(0x10, crt_membase + 0x4E120);

	writel((readl(crt_membase+0x1e0) & 0xffff9fff) , crt_membase+0x1e0);
	mdelay(1);
	writel((readl(crt_membase+0x1e0) | 0x00004000) , crt_membase+0x1e0);
	mdelay(1);
	writel((readl(crt_membase+0x1e0) | 0x00006000) , crt_membase+0x1e0);
	mdelay(1);
	writel((readl(crt_membase + 0x4E078) & 0xfdffefff) , crt_membase + 0x4E078);
	writel((readl(crt_membase + 0x4E07c) & 0xfff7ffbf) , crt_membase + 0x4E07c);
	writel((readl(crt_membase + 0x4E080) & 0xfdffefff) , crt_membase + 0x4E080);
#endif
}
EXPORT_SYMBOL(rtk_sdhci_platform_init);

void rtk_sdhci_close_clk(void)
{
	if (SDIO_module_disable == false) {
		clock_enable = false;
		G_host->mmc->caps2 |=
		    (MMC_CAP2_NO_SD | MMC_CAP2_NO_MMC | MMC_CAP2_NO_SDIO);

		if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619) {
			pr_err("Do not detect the SDIO card and disable sdio irq\n");
			disable_sdio_irq(G_host);
			mdelay(10);
		}

		pr_err("Do not detect the SDIO card and close the clock, mmc->caps2=0x%x\n",
		       G_host->mmc->caps2);
		writel(0x0, crt_membase + 0x01AC);
		reset_control_assert(rstc_sdio);
		clk_disable_unprepare(clk_en_sdio);
		clk_disable_unprepare(clk_en_sdio_ip);
		/* Ensure sdio irq disable correctly.*/
		wmb();
		mdelay(10);
		if (chip_id == CHIP_ID_RTD1619) {
			if (get_rtd_chip_revision() == RTD_CHIP_A01) {
				writel(readl(crt_membase + 0x1A0) & (~0x1),
					crt_membase + 0x1A0);
				writel(readl(crt_membase + 0x1A4) & (~0x1),
					crt_membase + 0x1A4);
				udelay(200);
			}
		} else if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319) {
			writel(readl(crt_membase + 0x7064) & ~(0x3 << 4), crt_membase + 0x7064);
			udelay(200);
		} else if (chip_id == CHIP_ID_RTD1619B) {
			writel(readl(crt_membase + 0x70c4) & ~(0x3 << 4), crt_membase + 0x70c4);
			udelay(200);
		}
	}
}
EXPORT_SYMBOL(rtk_sdhci_close_clk);

static void rtk_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
	mmc->card = card;
}

static int rtk_mmc_send_tuning(struct mmc_host *mmc, u32 opcode, int tx)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_command cmd = {0};
	int err;

	if (tx) {
		writew(0x1, host->ioaddr + 0x36);
		writew(0x1, host->ioaddr + 0x3A);

		cmd.opcode = opcode;
		cmd.arg = 0x2000;
		cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

		err = mmc_wait_for_cmd(mmc, &cmd, 0);
		if (err)
			return err;
	} else {
		return mmc_send_tuning(mmc, opcode, NULL);
	}

	return 0;
}

static int rtk_sdhci_change_phase(struct sdhci_host *host, u8 sample_point, int tx)
{
	unsigned int temp_reg = 0;
	u16 clk = 0;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		writel(readl(crt_membase + 0x1A0) & 0xfffffffd, crt_membase + 0x1A0);

	temp_reg = readl(crt_membase + 0x1A0);

	if (tx)
		temp_reg = (temp_reg & ~0x000000F8) | (sample_point << 3);
	else
		temp_reg = (temp_reg & ~0x00001F00) | (sample_point << 8);

	writel(temp_reg, crt_membase + 0x1A0);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		writel(readl(crt_membase + 0x1A0) | 0x2, crt_membase + 0x1A0);

	udelay(100);
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	return 0;
}

static u8 rtk_sdhci_search_final_phase(u32 phase_map)
{
	struct timing_phase_path path[MAX_PHASE + 1];
	struct timing_phase_path swap;
	int i = 0;
	int j = 0;
	int k = 0;
	int cont_path_cnt = 0;
	int new_block = 1;
	int max_len = 0;
	int final_path_idx = 0;
	u8 final_phase = 0xFF;

	/* Parse phase_map, take it as a bit-ring */
	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (phase_map & (1 << i)) {
			if (new_block) {
				new_block = 0;
				j = cont_path_cnt++;
				path[j].start = i;
				path[j].end = i;
			} else {
				path[j].end = i;
			}
		} else {
			new_block = 1;
			if (cont_path_cnt) {
				/* Calculate path length and middle point */
				int idx = cont_path_cnt - 1;

				path[idx].len =
				    path[idx].end - path[idx].start + 1;
				path[idx].mid =
				    path[idx].start + path[idx].len / 2;
			}
		}
	}

	if (cont_path_cnt == 0) {
		pr_err(" %s No continuous phase path\n", __func__);
		goto finish;
	} else {
		/* Calculate last continuous path length and middle point */
		int idx = cont_path_cnt - 1;

		path[idx].len = path[idx].end - path[idx].start + 1;
		path[idx].mid = path[idx].start + path[idx].len / 2;
	}

	/* Connect the first and last continuous paths if they are adjacent */
	if (!path[0].start && (path[cont_path_cnt - 1].end == MAX_PHASE)) {
		/* Using negative index */
		path[0].start = path[cont_path_cnt - 1].start - MAX_PHASE - 1;
		path[0].len += path[cont_path_cnt - 1].len;
		path[0].mid = path[0].start + path[0].len / 2;
		/* Convert negative middle point index to positive one */
		if (path[0].mid < 0)
			path[0].mid += MAX_PHASE + 1;
		cont_path_cnt--;
	}

	/* Sorting path array,jamestai20141223 */
	for (k = 0; k < cont_path_cnt; ++k) {
		for (i = 0; i < cont_path_cnt - 1 - k; ++i) {
			if (path[i].len < path[i + 1].len) {
				swap.end = path[i + 1].end;
				swap.len = path[i + 1].len;
				swap.mid = path[i + 1].mid;
				swap.start = path[i + 1].start;

				path[i + 1].end = path[i].end;
				path[i + 1].len = path[i].len;
				path[i + 1].mid = path[i].mid;
				path[i + 1].start = path[i].start;

				path[i].end = swap.end;
				path[i].len = swap.len;
				path[i].mid = swap.mid;
				path[i].start = swap.start;
			}
		}
	}

	/* Choose the longest continuous phase path */
	max_len = 0;
	final_phase = 0;
	final_path_idx = 0;
	for (i = 0; i < cont_path_cnt; i++) {
		/* for compatibility issue, continue len should more than 16 */
		if (path[i].len > max_len) {
			max_len = path[i].len;
			if (max_len > 16)
				final_phase = (u8) path[i].mid;
			else
				final_phase = 0xFF;
			final_path_idx = i;
		}
	}

finish:
	return final_phase;
}

static int rtk_sdhci_tuning(struct sdhci_host *host, u32 opcode, int tx)
{
	int sample_point = 0;
	int ret = 0;
	int i = 0;
	u32 raw_phase_map[TUNING_CNT] = { 0 };
	u32 phase_map = 0;
	u8 final_phase = 0;

	struct mmc_host *mmc = host->mmc;

	for (sample_point = 0; sample_point <= MAX_PHASE; sample_point++) {
		for (i = 0; i < TUNING_CNT; i++) {
			rtk_sdhci_change_phase(host, (u8) sample_point, tx);
			ret = rtk_mmc_send_tuning(mmc, opcode, tx);
			if (ret == 0)
				raw_phase_map[i] |= (1 << sample_point);
		}
	}

	phase_map = 0xFFFFFFFF;
	for (i = 0; i < TUNING_CNT; i++)
		phase_map &= raw_phase_map[i];

	pr_err("%s %s phase_map = 0x%08x\n", __func__, tx ? "TX" : "RX", phase_map);

	if (phase_map) {
		final_phase = rtk_sdhci_search_final_phase(phase_map);
		pr_err("%s final phase = 0x%08x\n", __func__, final_phase);
		if (final_phase == 0xFF) {
			pr_err("%s final phase = 0x%08x\n", __func__,
			       final_phase);
			ret = -EINVAL;
			goto out;
		}
		rtk_sdhci_change_phase(host, final_phase, tx);
		ret = 0;
		goto out;
	} else {
		pr_err("%s  fail !phase_map\n", __func__);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int rtk_sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	/* [ToDo] SDIO30 clock phase tuning, jamestai20151229 */
	int ret = 0;
	unsigned int reg_tmp = 0;
	unsigned int reg_tmp2 = 0;
	unsigned int reg_tuned3318 = 0;

	pr_err("%s : Execute Clock Phase Tuning\n", __func__);
	reg_tmp2 = readl(crt_membase + 0x01A4);	/* disable spectrum */
	/* PLL_SD2 clear [15:13] */
	writel((reg_tmp2 & 0xFFFF1FFF), crt_membase + 0x01A4);

	/* rtl8822bs won't stable with sdio clk 208Mhz. Adjust the clk to 200Mhz*/
	if(host->mmc->card->cis.device == 0x0000b822)
		rtk_sdhci_pll_configure(0x00ae4388, 1);

#ifdef sdhci_tx_tuning
	ret = rtk_sdhci_tuning(host, SD_IO_RW_DIRECT, 1);
#endif

	do {
		ret = rtk_sdhci_tuning(host, MMC_SEND_TUNING_BLOCK, 0);
		if (ret) {
			reg_tmp = readl(crt_membase + 0x01A8);
			reg_tuned3318 = (reg_tmp & 0x03FF0000) >> 16;
			if (reg_tuned3318 == 158) {
				/* When PLL set to 96, may interference wifi 2.4Ghz */
				reg_tuned3318 = reg_tuned3318 - 8;
			} else if (reg_tuned3318 <= 100) {
				pr_err("%s: Tuning RX fail\n", __func__);
				return ret;
			}
			/* down 8MHz */
			reg_tmp = ((reg_tmp & (~0x3FF0000)) |
				   ((reg_tuned3318 - 8) << 16));
			rtk_sdhci_pll_configure(reg_tmp, 1);
		}
	} while (ret);

	writel(reg_tmp2, crt_membase + 0x01A4);

	pr_err("After tuning, current SDIO PLL = %x\n",
	       (readl(crt_membase + 0x01A8) & 0x03FF0000) >> 16);

	return 0;
}

static void rtk_replace_mmc_host_ops(struct sdhci_host *host)
{
	host->mmc_host_ops.init_card = rtk_init_card;
}

static const struct sdhci_ops rtk_sdhci_ops = {
	.reset = rtk_sdhci_reset,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = rtk_sdhci_uhs_signaling,
	.set_clock = rtk_sdhci_set_clock,
	.platform_execute_tuning = rtk_sdhci_execute_tuning,
};

static const struct sdhci_pltfm_data sdhci_rtk_sdio_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
	    SDHCI_QUIRK_SINGLE_POWER_WRITE |
	    SDHCI_QUIRK_NO_HISPD_BIT |
	    SDHCI_QUIRK_BROKEN_CARD_DETECTION |
	    SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC,
	.quirks2 = SDHCI_QUIRK2_BROKEN_DDR50 | SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &rtk_sdhci_ops,
};

static struct sdhci_rtk_sdio_data sdhci_rtk_sdio_data = {
	.pdata = &sdhci_rtk_sdio_pdata,
};

static const struct of_device_id sdhci_rtk_dt_match[] = {
	{.compatible = "realtek,rtd1295-sdio", .data = &sdhci_rtk_sdio_data},
	{.compatible = "realtek,rtd1319-sdio", .data = &sdhci_rtk_sdio_data},
	{.compatible = "realtek,rtd1619b-sdio", .data = &sdhci_rtk_sdio_data},
	{}
};

MODULE_DEVICE_TABLE(of, sdhci_rtk_dt_match);

static int sdhci_rtk_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sdhci_rtk_sdio_data *soc_data;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	int rc = 0;

	struct device_node *node = pdev->dev.of_node;

	pr_info("%s: build at : %s\n", DRIVER_NAME, utsname()->version);

	chip_id = get_rtd_chip_id();

	drive_temp = 0;
	SDIO_module_disable = false;
	clock_enable = true;
	SDIO_card = true;

	sdio_membase = of_iomap(node, 0);
	if (!sdio_membase)
		return -ENOMEM;

	crt_membase = of_iomap(node, 1);
	if (!crt_membase)
		return -ENOMEM;

	sdio_gpio_23 = devm_gpiod_get(&pdev->dev, "sdio", GPIOD_OUT_LOW);
	if (IS_ERR(sdio_gpio_23)) {
		dev_err(&pdev->dev, "sdio gpio missing or invalid\n");
	} else {
		gpiod_direction_output(sdio_gpio_23, 0);
		mdelay(150);
		gpiod_direction_input(sdio_gpio_23);
	}

	match = of_match_device(sdhci_rtk_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	soc_data = match->data;

	rstc_sdio = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rstc_sdio)) {
		pr_warn("Failed to get sdio reset control(%ld)\n", PTR_ERR(rstc_sdio));
		rstc_sdio = NULL;
	}

	clk_en_sdio = devm_clk_get(&pdev->dev, "sdio");
	if (IS_ERR(clk_en_sdio)) {
		pr_warn("Failed to get sdio clk(%ld)\n", PTR_ERR(clk_en_sdio));
		clk_en_sdio = NULL;
	}

	clk_en_sdio_ip = devm_clk_get(&pdev->dev, "sdio_ip");
	if (IS_ERR(clk_en_sdio_ip)) {
		pr_warn("Failed to get sdio ip clk(%ld)\n", PTR_ERR(clk_en_sdio_ip));
		clk_en_sdio_ip = NULL;
	}

	host = sdhci_pltfm_init(pdev, soc_data->pdata, 0);
	if (IS_ERR(host))
		return PTR_ERR(host);

	rc = reset_control_deassert(rstc_sdio);
	if (rc) {
		pr_warn("Can not deassert sdio reset\n");
		goto err_add_host;
	}

	rc = clk_prepare_enable(clk_en_sdio);
	if (rc) {
		pr_warn("Failed to enable sdio clock\n");
		goto err_add_host;
	}

	rc = clk_prepare_enable(clk_en_sdio_ip);
	if (rc) {
		pr_warn("Failed to enable sdio ip clock\n");
		goto err_sdio_clk;
	}

	rtk_sdhci_platform_init();

	pltfm_host = sdhci_priv(host);

	rtk_replace_mmc_host_ops(host);

	rc = mmc_of_parse(host->mmc);
	if (rc)
		goto err_sdio_ip_clk;

	G_host = host;

	rc = sdhci_add_host(host);
	if (rc)
		goto err_sdio_ip_clk;

	return 0;

err_sdio_ip_clk:
	clk_disable_unprepare(clk_en_sdio_ip);
err_sdio_clk:
	clk_disable_unprepare(clk_en_sdio);
err_add_host:
	sdhci_pltfm_free(pdev);
	return rc;
}

static int sdhci_rtk_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xFFFFFFFF);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}

static int sdhci_rtk_suspend(struct device *dev)
{
	pr_err("[SDIO] %s start\n", __func__);

	if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
		drive_temp = readl(crt_membase + 0x01A0);

	if (clock_enable == true) {
		struct sdhci_host *host = dev_get_drvdata(dev);

		sdhci_suspend_host(host);
	}
	pr_err("[SDIO] %s OK\n", __func__);
	return 0;
}

static void sdhci_rtk_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pr_err("[SDIO] %s\n", __func__);
	if (clock_enable == true) {
		struct sdhci_host *host = dev_get_drvdata(dev);

		sdhci_suspend_host(host);
	}
}

static int sdhci_rtk_resume(struct device *dev)
{

	struct sdhci_host *host = dev_get_drvdata(dev);
	void __iomem *addr;
	unsigned int val;

	host->clock = 0;

	pr_err("[SDIO] %s start\n", __func__);
	if (clock_enable == true) {
		addr = crt_membase + SDIO_CLKEN_REGOFF;

		if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1319)
			writel(readl(crt_membase + 0x10A58) | 0x80000000, crt_membase + 0x10A58);

		if (get_SDIO_version() == 3 || get_SDIO_version() == 4) {
			reset_control_deassert(rstc_sdio);
			if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
				writel(drive_temp, crt_membase + 0x01A0);

			rtk_sdhci_pad_driving_configure();

			if (get_SDIO_version() == 3) {
				rtk_sdhci_pll_configure(0x00564388, 0);
			} else {
				/* enlarge wait time for data */
				if (chip_id == CHIP_ID_RTD1619)
					writel(0x1d, crt_membase + 0x10a58);
				/* rtl8822bs won't stable with sdio clk 208Mhz. Adjust the clk to 200Mhz*/
				if(host->mmc->card->cis.device == 0x0000b822)
					rtk_sdhci_pll_configure(0x00ae4388, 0);
				else
					rtk_sdhci_pll_configure(0x00b64388, 0);
			}
			if (chip_id != CHIP_ID_RTD1619B)
				writel(0x00000000, crt_membase + 0x010A40);
			writel(0x00000011, crt_membase + 0x010A34);
		} else {
			reset_control_deassert(rstc_sdio);
			rtk_sdhci_pll_configure(0x00ae4388, 0);
			writel(0x00000011, crt_membase + 0x010A34);
		}

		if (chip_id == CHIP_ID_RTD1312C || chip_id == CHIP_ID_RTD1619B || chip_id == CHIP_ID_RTD1319 || chip_id == CHIP_ID_RTD1619)
			writel(0x00000001, crt_membase + 0x010A10);
		else
			writel(0x00000003, crt_membase + 0x010A10);

		val = readl(addr);
		val |= 1 << (SDIO_CLKEN_REGBIT);
		writel(val, addr);

		val = readl(host->ioaddr + 0x28);
		val |= 0x0f << 8;
		writel(val, host->ioaddr + 0x28);
		val = readl(host->ioaddr + 0x28);

		sdhci_resume_host(host);
	}

	pr_err("[SDIO] %s OK\n", __func__);

	return 0;
}

const struct dev_pm_ops sdhci_rtk_pmops = {
	.suspend = sdhci_rtk_suspend,
	.resume = sdhci_rtk_resume,
};

static struct platform_driver sdhci_rtk_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sdhci_rtk_dt_match,
		.pm = &sdhci_rtk_pmops,
		},
	.probe = sdhci_rtk_probe,
	.remove = sdhci_rtk_remove,
	.shutdown = sdhci_rtk_shutdown,
};
module_platform_driver(sdhci_rtk_driver);
