// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)

/*
 * Realtek EMMC host driver
 *
 * Copyright (c) 2019-2020 Realtek Semiconductor Corp.
 */

#include <asm/cacheflush.h>
#include <asm/unaligned.h>
#include <linux/arm-smccc.h>
#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mbus.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <crypto/hash.h>
#include <soc/realtek/rtk_chip.h>
#include <trace/events/mmc.h>

#include "../core/card.h"
#include "../core/host.h"
#include "cqhci.h"

#include "reg_iso.h"
#include "rtkemmc_rtd13xx.h"
#include "mmc_debug.h"

#define DRIVER_NAME	"EMMC"
#define BANNER		"Realtek eMMC Driver"
#define VERSION 	"$Id: rtkemmc.c Hank 2019-1-7 19:00 $"

#define DMA_ALLOC_LENGTH     (0x80000)
#define DESC_ALLOC_LENGTH   (1024*1024)
unsigned int GLOBAL=0;
#define PHASE_INHERITED

#define FORCE_CHECK_CMD_AND_STS

#define DQS_INHERITED

#define TIMEOUT_MS 3000
#define TIMEOUT_DMA 50
#define TIMEOUT_CMD 10000 //10 secs

#define TUNING_STAGE1     1
#define TUNING_STAGE2     2
#define TUNING_STAGE_BOTH 3

//#define RTKEMMC_DEBUG
#define RTKEMMC_PHASE_TRACE

#ifdef RTKEMMC_PHASE_TRACE
u32 trace_TX_window=0;
u32 trace_RX_window=0;
u32 trace_TX1_window=0;
u32 trace_cur_tuning_cmd=0;
u32 trace_index=0;
u32 trace_phase_bitmap=0;
u32 trace_dqs_counter=0;
u32 trace_desc_counter=0;
u16 trace_err_status=0;
u16 trace_auto_err_status=0;
u16 trace_normal_status=0;
u16 trace_dqs_index=0;
#endif

static volatile int g_bResuming;
volatile int g_bTuning;

#define SHA256

#define MMC_SEND_WRITE_PROT_TYPE 31
#define MMC_MANUFACTURER_CMD62 62
#define MMC_MANUFACTURER_CMD63 63

#ifdef SHA256
struct sdesc {
	struct shash_desc shash;
	char ctx[];
};
static int rtk_get_hash(unsigned char *input, unsigned char *sha256_hash, unsigned int dma_len);
unsigned char *compare3=NULL;
unsigned char *compare4=NULL;
dma_addr_t          compare3_phy_addr;
dma_addr_t          compare4_phy_addr;
#else
unsigned char compare1[DMA_ALLOC_LENGTH];
unsigned char compare2[DMA_ALLOC_LENGTH];
#endif

static void rtkemmc_request(struct mmc_host *host, struct mmc_request *mrq);
static void rtkemmc_set_ios(struct mmc_host *host, struct mmc_ios *ios);
static void rtkemmc_init_card(struct mmc_host *host, struct mmc_card *card);
static int rtkemmc_hs400_prepare_ddr(struct mmc_host *host);
static void set_cmd_info(struct mmc_card *card,struct mmc_command * cmd,
struct sd_cmd_pkt * cmd_info,u32 opcode,u32 arg,u8 rsp_para);

static int rtkemmc_stop_transmission(struct mmc_card *card,int bIgnore);
static int rtkemmc_wait_status(struct mmc_card *card,u8 state,u8 divider,int bIgnore);
int rtkemmc_send_cmd6(struct rtkemmc_host *emmc_port, u32 args,u16 * state, int phase);
static int rtkemmc_send_cmd13(struct rtkemmc_host *emmc_port, u16 * state);

static int mmc_Tuning_DDR50(struct rtkemmc_host *emmc_port);
static int mmc_Tuning_HS200(struct rtkemmc_host *emmc_port);
static int rtkemmc_execute_tuning(struct mmc_host *host, u32 opcode);
static int rtkemmc_prepare_hs400_tuning(struct mmc_host *host, struct mmc_ios *ios);
void phase(struct rtkemmc_host *emmc_port, u32 VP0, u32 VP1);
static void rtkemmc_init(struct rtkemmc_host *emmc_port);
static void rtkemmc_set_pin_mux(struct rtkemmc_host *emmc_port);
static void rtkemmc_set_freq(struct rtkemmc_host *emmc_port, u32 freq, u32 div_ip);
static void rtkemmc_stark_set_freq(struct rtkemmc_host *emmc_port, u32 freq, u32 div_ip);
void error_handling(struct rtkemmc_host *emmc_port);
static int SD_SendCMDGetRSP(struct sd_cmd_pkt * cmd_info,int bIgnore);
static int SD_Stream(struct sd_cmd_pkt *cmd_info, unsigned int bIgnore);
void rtkemmc_set_pad_driving(struct rtkemmc_host *emmc_port, u32 clk_drv, u32 cmd_drv, u32 data_drv, u32 ds_drv);
static int wait_done_timeout(struct rtkemmc_host *emmc_port, volatile u32 *addr, u32 mask, u32 value,const char *string);
static void rtkemmc_dqs_tuning(struct mmc_host *host);
int rtkemmc_set_blocklen(struct mmc_card* card, unsigned int blocklen);
int rtkemmc_write_protect_cmd(struct rtkemmc_host *emmc_port, u32 args, bool is_wrtie_protect);
int rtkemmc_query_protect_cmd(struct rtkemmc_host *emmc_port, unsigned long addr, u32 cmd_idx);

DECLARE_COMPLETION(rtk_emmc_wait);
typedef void (*set_gpio_func_t)(u32 gpio_num,u8 dir,u8 level);
int rtkemmc_switch(struct rtkemmc_host *emmc_port, u8 set, u8 index, u8 value, unsigned int timeout_ms);
int suspend_VP0=0xff, suspend_VP1=0xff;
int HS200_RX=0xff, HS200_TX=0xff;
int suspend_dqs=0x88;
static u32 HS400_VERSION = 2;

void mmc_get_card(struct mmc_card *card, struct mmc_ctx *ctx);
void mmc_put_card(struct mmc_card *card, struct mmc_ctx *ctx);

static const struct mmc_host_ops rtkemmc_ops = {
	.request        = rtkemmc_request,
	.set_ios        = rtkemmc_set_ios,
	.execute_tuning = rtkemmc_execute_tuning,
	.prepare_hs400_tuning = rtkemmc_prepare_hs400_tuning,
	.hs400_complete = rtkemmc_dqs_tuning,
	.hs400_prepare_ddr = rtkemmc_hs400_prepare_ddr,
	.init_card = rtkemmc_init_card
};

static const struct soc_device_attribute rtk_soc_groot[] = {
	{
		.family = "Realtek Groot",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_stark[] = {
	{
		.family = "Realtek Stark",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_hank[] = {
	{
		.family = "Realtek Hank",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_hank_a00[] = {
	{
		.family = "Realtek Hank",
		.revision = "A00",
	},
	{
		/* empty */
	}
};

// write addr with val
void rtkemmc_write_swc(u32 addr, u32 val)
{
	struct arm_smccc_res res;

	arm_smccc_smc(0x8400ffff, addr, val, 0, 0, 0, 0, 0, &res);
}

// read addr
u32 rtkemmc_read_swc(u32 addr)
{
	struct arm_smccc_res res;
	arm_smccc_smc(0x8400fffe, addr, 0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

void sync(struct rtkemmc_host *emmc_port)
{
	dmb(sy);
	writel(0x0, emmc_port->sb2_membase+0x20);
	dmb(sy);
}

static void rtkemmc_reset_fifo(struct rtkemmc_host *emmc_port)
{
	writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&(~EMMC_TOP_RST_N_FIFO),
		emmc_port->emmc_membase+EMMC_OTHER1);
	udelay(1);
	writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)|EMMC_TOP_RST_N_FIFO,
		emmc_port->emmc_membase+EMMC_OTHER1);
}

static void rtk_cqe_enable(struct mmc_host *host);
static void rtk_cqhci_dumpregs(struct mmc_host *host);
static void rtk_cqhci_setup_tran_desc(struct mmc_data *data, struct cqhci_host *cq_host, u8 *desc, int sg_count);
static void rtkemmc_cqhci_set_tran_desc(u8 *desc, dma_addr_t addr, int len, bool end, bool dma64);

static const struct cqhci_host_ops rtk_cqhci_host_ops = {
	.enable = rtk_cqe_enable,
	.dumpregs = rtk_cqhci_dumpregs,
	.setup_tran_desc = rtk_cqhci_setup_tran_desc,
};

static void rtk_cqhci_dumpregs(struct mmc_host *host)
{
	struct rtkemmc_host *emmc_port;
	emmc_port = mmc_priv(host);

	pr_err("%s: Cmd idx 0x%08x\n",__func__, readw(emmc_port->emmc_membase+EMMC_CMD_R));
}

static void rtk_cqhci_setup_tran_desc(struct mmc_data *data,
                               struct cqhci_host *cq_host, u8 *desc, int sg_count)
{
	int i, len;
	bool end = false;
	bool dma64 = cq_host->dma64;
	dma_addr_t addr;
	struct scatterlist *sg;
	u32 blk_cnt2, remain_blk_cnt;
	unsigned int b1, b2;

	for_each_sg(data->sg, sg, sg_count, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
		remain_blk_cnt  = len >> 9;

		while(remain_blk_cnt) {
			if(remain_blk_cnt > EMMC_MAX_SCRIPT_BLK)
				blk_cnt2 = EMMC_MAX_SCRIPT_BLK;
			else
				blk_cnt2 = remain_blk_cnt;
			//boundary check
			b1 = addr / 0x8000000;              //this eMMC ip dma transfer has 128MB limitation
			b2 = (addr+blk_cnt2*512) / 0x8000000;

			if(b1 != b2) {
				blk_cnt2 = (b2*0x8000000-addr) / 512;
			}
			if ((i+1) == sg_count && (remain_blk_cnt == blk_cnt2))
				end = true;
			rtkemmc_cqhci_set_tran_desc(desc, addr, (blk_cnt2<<9), end, dma64);

			addr = addr+(blk_cnt2<<9);
			remain_blk_cnt -= blk_cnt2;
			desc += cq_host->trans_desc_len;
		}
	}
}

static void rtk_cqe_enable(struct mmc_host *host)
{
	struct rtkemmc_host *emmc_port;
	emmc_port = mmc_priv(host);

	//clear data path SW_RST_R.SW_RST_DAT = 1
	rtkemmc_writeb(EMMC_SW_RST_DAT, emmc_port->emmc_membase+EMMC_SW_RST_R);
	//0x98012004,Set value corresponding to executed data byte length of one block to BLOCKSIZE_R
	rtkemmc_writew(0x200, emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
	rtkemmc_writew(((1<<EMMC_MULTI_BLK_SEL)|EMMC_BLOCK_COUNT_ENABLE|EMMC_DMA_ENABLE),
		emmc_port->emmc_membase+EMMC_XFER_MODE_R);     //0x9801200c
	//Set DMA_SEL to ADMA2 only mode in the HOST_CTRL1_R
        rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R)&0xe7)|(EMMC_ADMA2_32<<EMMC_DMA_SEL),
                        emmc_port->emmc_membase+EMMC_HOST_CTRL1_R);
        rtkemmc_writew(0, emmc_port->emmc_membase+EMMC_BLOCKCOUNT_R);
	//Set SDMASA_R (while using 32 bits) to 0
        rtkemmc_writel(0, emmc_port->emmc_membase+EMMC_SDMASA_R);
	//we set this register additionally to enhance the IO perofrmance
	rtkemmc_writel(0x10, emmc_port->cq_host->mmio+CQHCI_SSC1);

	rtkemmc_writel(0, emmc_port->cq_host->mmio+CQHCI_CTL);
	if (readl(emmc_port->cq_host->mmio+CQHCI_CTL) && CQHCI_HALT) {
		pr_err("%s: cqhci: CQE failed to exit halt state\n",
			mmc_hostname(emmc_port->mmc));
	}

        //cmdq interrupt mode
	rtkemmc_hold_int_dec();
	rtkemmc_en_cqe_int();

        sync(emmc_port);
}

static void rtkemmc_cqhci_set_tran_desc(u8 *desc, dma_addr_t addr, int len, bool end,
                                bool dma64)
{
	__le32 *attr = (__le32 __force *)desc;

	*attr = (CQHCI_VALID(1) |
		 CQHCI_END(end ? 1 : 0) |
		 CQHCI_INT(0) |
		 CQHCI_ACT(0x4) |
		 CQHCI_DAT_LENGTH(len));

	if (dma64) {
		__le64 *dataddr = (__le64 __force *)(desc + 4);

		dataddr[0] = cpu_to_le64(addr);
	} else {
		__le32 *dataddr = (__le32 __force *)(desc + 4);

		dataddr[0] = cpu_to_le32(addr);
	}
}

static int rtkemmc_blk_cmdq_switch(struct mmc_card* card, bool enable)
{
        int ret = 0;
	if (!(card->host->caps2 & MMC_CAP2_CQE) ||
                !card->ext_csd.cmdq_support)
                return 0;

        if (enable) {
                ret = rtkemmc_set_blocklen(card, 512);
                if (ret) {
                        pr_err("%s: failed (%d) to set block-size\n",
                                __func__, ret);
                        goto out;
                }
        }
        ret = rtkemmc_switch(mmc_priv(card->host), EXT_CSD_CMD_SET_NORMAL,
                        EXT_CSD_CMDQ_MODE_EN, enable,
                        card->ext_csd.generic_cmd6_time);
        if (ret) {
                pr_err("%s: cmdq mode %sable failed %d\n", __func__, enable ? "en" : "dis", ret);
                goto out;
        }
	else
		card->ext_csd.cmdq_en = enable;

out:
        return ret;
}

static void rtkemmc_cqhci_dumpregs(struct cqhci_host *cq_host)
{
	struct mmc_host *host = cq_host->mmc;
	struct rtkemmc_host *emmc_port;
	emmc_port = mmc_priv(host);

	pr_err("============ CQHCI REGISTER DUMP ===========\n");

	pr_err("Caps:      0x%08x | Version:  0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_CAP),
		   cqhci_readl(cq_host, CQHCI_VER));
	pr_err("Config:    0x%08x | Control:  0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_CFG),
                   cqhci_readl(cq_host, CQHCI_CTL));
	pr_err("Int stat:  0x%08x | Int enab: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_IS),
                   cqhci_readl(cq_host, CQHCI_ISTE));
	pr_err("Int sig:   0x%08x | Int Coal: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_ISGE),
                   cqhci_readl(cq_host, CQHCI_IC));
	pr_err("TDL base:  0x%08x | TDL up32: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_TDLBA),
                   cqhci_readl(cq_host, CQHCI_TDLBAU));
	pr_err("Doorbell:  0x%08x | TCN:      0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_TDBR),
                   cqhci_readl(cq_host, CQHCI_TCN));
	pr_err("Dev queue: 0x%08x | Dev Pend: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_DQS),
                   cqhci_readl(cq_host, CQHCI_DPT));
	pr_err("Task clr:  0x%08x | SSC1:     0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_TCLR),
                   cqhci_readl(cq_host, CQHCI_SSC1));
	pr_err("SSC2:      0x%08x | DCMD rsp: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_SSC2),
                   cqhci_readl(cq_host, CQHCI_CRDCT));
	pr_err("RED mask:  0x%08x | TERRI:    0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_RMEM),
                   cqhci_readl(cq_host, CQHCI_TERRI));
	pr_err("Resp idx:  0x%08x | Resp arg: 0x%08x\n",
                   cqhci_readl(cq_host, CQHCI_CRI),
                   cqhci_readl(cq_host, CQHCI_CRA));

	pr_err("%s: Cmd idx 0x%08x\n",__func__, readw(emmc_port->emmc_membase+EMMC_CMD_R));
}

void rtkemmc_dqs_delay_tap(struct rtkemmc_host *emmc_port, u32 dqs_dly)
{
	if(soc_device_match(rtk_soc_hank_a00)) {	//hank A00
		//this is a workaround for hank hs400 mode and will be fixed in A01
		u16 state=0;
		unsigned long timeout;
		bool expired = false;
		int err=0;

		rtkemmc_send_cmd6(emmc_port, 0x03b90301, &state, dqs_dly);
		rtkemmc_writel(0x80|dqs_dly ,emmc_port->emmc_membase + EMMC_DQS_CTRL1);
		rtkemmc_send_cmd6(emmc_port, 0x03b91301, &state, dqs_dly);

		timeout = jiffies + msecs_to_jiffies(3000) + 1;
		do {
			expired = time_after(jiffies, timeout);
			err = rtkemmc_send_cmd13(emmc_port, &state);
			if (expired &&
				(R1_CURRENT_STATE(state) == R1_STATE_PRG)) {
				pr_err("%s: Card stuck in programming state! %s\n",
					mmc_hostname(emmc_port->mmc), __func__);
				break;
			}
		}while(R1_CURRENT_STATE(state) == R1_STATE_PRG);
	}
	else if(soc_device_match(rtk_soc_hank)){	//hank
		rtkemmc_writel(0x80|dqs_dly ,emmc_port->emmc_membase + EMMC_DQS_CTRL1);
	}
	else {	//stark
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_DQS_CTRL1)&(~(1<<8)),
			emmc_port->emmc_membase+EMMC_DQS_CTRL1);
		rtkemmc_writel(dqs_dly ,emmc_port->emmc_membase + EMMC_DQS_CTRL1);
		rtkemmc_writel(0x100|dqs_dly ,emmc_port->emmc_membase + EMMC_DQS_CTRL1);
	}
}

static void data_delay_tap_setting(struct rtkemmc_host *emmc_port)
{
	if(soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)) {
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL0)&(~EMMC_FW_SET),
			emmc_port->emmc_membase+EMMC_RDQ_CTRL0);
		if(emmc_port->dqs_dly_tape==0) return;

		rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL0);
	}
	else {
		rtkemmc_writel(EMMC_FW_SET | emmc_port->dqs_dly_tape,
			emmc_port->emmc_membase+EMMC_RDQ_CTRL0);
	}

	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL1);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL2);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL3);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL4);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL5);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL6);
	rtkemmc_writel(emmc_port->dqs_dly_tape, emmc_port->emmc_membase+EMMC_RDQ_CTRL7);

	if(soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)){
		rtkemmc_writel(EMMC_FW_SET | readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL0),
                        emmc_port->emmc_membase+EMMC_RDQ_CTRL0);
	}
	else
		rtkemmc_writel(EMMC_FW_SET_RW, emmc_port->emmc_membase+EMMC_DQ_CTRL_SET);
}

static void cmd_delay_tap_setting(struct rtkemmc_host *emmc_port, u32 cmd_dly_tape)
{
	if(soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)) {
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_WCMD_CTRL)&(~(1<<7)),
			emmc_port->emmc_membase+EMMC_WCMD_CTRL);

		if(cmd_dly_tape==0) return;

		rtkemmc_writel(cmd_dly_tape, emmc_port->emmc_membase + EMMC_WCMD_CTRL);
		rtkemmc_writel(cmd_dly_tape|0x80, emmc_port->emmc_membase+EMMC_WCMD_CTRL);
	}
	else {
		rtkemmc_writel(cmd_dly_tape, emmc_port->emmc_membase + EMMC_WCMD_CTRL);
		if(cmd_dly_tape != 0)
			rtkemmc_writel(0x1, emmc_port->emmc_membase + EMMC_CMD_CTRL_SET);
	}
}

static int rtkemmc_prepare_hs400_tuning(struct mmc_host *host, struct mmc_ios *ios)
{
	struct rtkemmc_host *emmc_port;
        emmc_port = mmc_priv(host);
	printk(KERN_ERR "Prepare HS400 mode...\n");

	rtkemmc_dqs_delay_tap(emmc_port, 0x88);

	data_delay_tap_setting(emmc_port);

	if(emmc_port->dqs_tuning == 0) {
		printk(KERN_ERR "%s: write the saved cmd_dqs_tap 0x%x\n", __func__, emmc_port->cmd_dly_tap);
		cmd_delay_tap_setting(emmc_port, emmc_port->cmd_dly_tap);
	}
	else
		cmd_delay_tap_setting(emmc_port, 0);

	return 0;
}

static void set_cmd_info(struct mmc_card *card,struct mmc_command * cmd, struct sd_cmd_pkt * cmd_info,u32 opcode,u32 arg,u8 rsp_para)
{
	memset(cmd, 0, sizeof(struct mmc_command));
	memset(cmd_info, 0, sizeof(struct sd_cmd_pkt));

	cmd->opcode         = opcode;
	cmd->arg            = arg;
	cmd_info->cmd       = cmd;
	cmd_info->emmc_port    = mmc_priv(card->host);
	cmd_info->rsp_len   = rsp_para;
}

#ifdef RTKEMMC_PHASE_TRACE
void print_desc_content(struct rtkemmc_host *emmc_port)
{
	int i =0;
	u32* des_base = emmc_port->desc_vaddr ;

	printk(KERN_ERR "%s: descriptor content:\n", __func__);
	for(i=0; i<trace_desc_counter; i+=2) {
		printk(KERN_ERR "des_base[%d]=0x%x, des_base[%d]=0x%x\n", i, des_base[i], i+1, des_base[i+1]);
	}
}
#endif

void print_reg_info(struct rtkemmc_host *emmc_port)
{
	pr_err("0x98012034 NORMAL INTERRUPT EN= 0x%x\n",readw(emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_EN_R));
	pr_err("0x98012036 ERROR INTERRUPT EN= 0x%x\n",readw(emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_EN_R));
	pr_err("0x98012038 NORMAL INTERRUPT SIGNAL EN= 0x%x\n",readw(emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R));
	pr_err("0x9801203a ERROR INTERRUPT SIGNAL EN = 0x%x\n",readw(emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R));
	pr_err("0x9801203c EMMC_AUTO_CMD_STAT_R = 0x%08x\n", readw(emmc_port->emmc_membase+EMMC_AUTO_CMD_STAT_R));
	pr_err("0x9801203e HOST CONTROL2 REG = 0x%x\n",readw(emmc_port->emmc_membase+EMMC_HOST_CTRL2_R));
	pr_err("0x98012024 PRESENT STATE REG = 0x%x\n",readl(emmc_port->emmc_membase+EMMC_PSTATE_REG));
	pr_err("0x98012028 HOST CONTROL1 REG= 0x%x\n",readb(emmc_port->emmc_membase+EMMC_HOST_CTRL1_R));
	pr_err("0x9801200c TRANSFER MODE REG = 0x%x\n",readw(emmc_port->emmc_membase+EMMC_XFER_MODE_R));
	pr_err("0x98012004 EMMC BLOCK SIZE = 0x%x\n",readw(emmc_port->emmc_membase+EMMC_BLOCKSIZE_R));
	pr_err("0x98012006 EMMC BLOCK COUNT = 0x%08x\n", readw(emmc_port->emmc_membase+EMMC_BLOCKCOUNT_R));
	pr_err("0x9801200e CMD_IDX = 0x%08x\n",readw(emmc_port->emmc_membase+EMMC_CMD_R));
        pr_err("0x98012008 EMMC CMDARG = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_ARGUMENT_R));
}

void print_err_reg(u32 cmd_idx, u16 normal_interrupt, u16 error_interrupt)
{
	pr_err("=====================================================\n");
	pr_err("g_bTuning=%d cmd_idx 0x%08x, op_code (%d)\n",g_bTuning, cmd_idx, CMD_IDX_MASK(cmd_idx));
	pr_err("0x98012030 NORMAL INTERRUPT STAT = 0x%x\n", normal_interrupt);
	pr_err("0x98012032 ERROR INTERRUPT STAT = 0x%x\n", error_interrupt);
	if( error_interrupt & EMMC_VENDOR_ERR3 ) {           pr_err("bit 15: EMMC_VENDOR_ERR3\n");       }
	if( error_interrupt & EMMC_VENDOR_ERR2 ) {           pr_err("bit 14: EMMC_VENDOR_ERR2\n");  }
	if( error_interrupt & EMMC_VENDOR_ERR1 ) {       pr_err("bit 13: EMMC_VENDOR_ERR1\n");  }
	if( error_interrupt & EMMC_BOOT_ACK_ERR ) {           pr_err("bit 12: EMMC_BOOT_ACK_ERR\n");       }
	if( error_interrupt & EMMC_RESP_ERR ) {          pr_err("bit 11: EMMC_RESP_ERR\n");    }
	if( error_interrupt & EMMC_TUNING_ERR ) {           pr_err("bit 10: EMMC_TUNING_ERR\n");     }
	if( error_interrupt & EMMC_ADMA_ERR ) {      pr_err("bit  9: EMMC_ADMA_ERR\n"); }
	if( error_interrupt & EMMC_AUTO_CMD_ERR ) {       pr_err("bit  8: EMMC_AUTO_CMD_ERR\n");  }
	if( error_interrupt & EMMC_CUR_LMT_ERR ) {          pr_err("bit  7: EMMC_CUR_LMT_ERR\n");    }
	if( error_interrupt & EMMC_DATA_END_BIT_ERR ) {          pr_err("bit  6: EMMC_DATA_END_BIT_ERR\n");        }
	if( error_interrupt & EMMC_DATA_CRC_ERR ) {          pr_err("bit  5: EMMC_DATA_CRC_ERR\n");   }
	if( error_interrupt & EMMC_DATA_TOUT_ERR ) {          pr_err("bit  4: EMMC_DATA_TOUT_ERR\n");  }
	if( error_interrupt & EMMC_CMD_IDX_ERR ) {           pr_err("bit  3: EMMC_CMD_IDX_ERR\n");   }
	if( error_interrupt & EMMC_CMD_END_BIT_ERR ) {            pr_err("bit  2: EMMC_CMD_END_BIT_ERR\n");  }
	if( error_interrupt & EMMC_CMD_CRC_ERR ) {            pr_err("bit  1: EMMC_CMD_CRC_ERR\n");    }
	if( error_interrupt & EMMC_CMD_TOUT_ERR ) {           pr_err("bit  0: EMMC_CMD_TOUT_ERR\n");   }
	pr_err("=====================================================\n");
}

void print_ip_desc(struct rtkemmc_host *emmc_port)
{ 
	pr_err("------------------------------>\n");
	pr_err("EMMC IP_DESC0 = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_IP_DESC0));
	pr_err("EMMC IP_DESC1 = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_IP_DESC1));
	pr_err("EMMC IP_DESC2 = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_IP_DESC2));
	pr_err("EMMC IP_DESC3 = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_IP_DESC3));

	pr_err("0x98012054 EMMC_ADMA_ERR_STAT_R = 0x%08x\n", readl(emmc_port->emmc_membase+EMMC_ADMA_ERR_STAT_R));
	pr_err("0x98012058 EMMC EMMC_ADMA_SA_LOW_R = 0x%08x\n------------------------------>\n", readl(emmc_port->emmc_membase+EMMC_ADMA_SA_LOW_R));
}

static void rtkemmc_shutdown(struct platform_device *pdev)
{
	//struct device *dev = &pdev->dev;

	printk(KERN_ERR "[eMMC] Shutdown\n");
	pm_runtime_force_suspend(&pdev->dev);
}

#ifdef CONFIG_PM
static int rtkemmc_suspend(struct device *dev)
{
	int ret = 0;
	struct rtkemmc_host *emmc_port=NULL;
	struct mmc_host *mmc = NULL;
	mmc = dev_get_drvdata(dev);
	emmc_port = mmc_priv(mmc);

	if(!mmc || !emmc_port)
		pr_err("mmc=NULL or emmc_port=NULL\n");

	emmc_port->suspend = 1;
	if(soc_device_match(rtk_soc_hank_a00)) {	//in hank A00 version
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)|0x1, emmc_port->emmc_membase+EMMC_OTHER1);        //disable L4 gated
		isb();
		sync(emmc_port);
	}

	if(mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(mmc);
		if (ret) {
			pr_err("%s: cqe suspend failed\n", __func__);
			return ret;
		}
	}
	ret = pm_runtime_force_suspend(dev);
	rtkemmc_writel(0, emmc_port->emmc_membase+EMMC_AHB);
	printk(KERN_ERR "[%s] Exit %s, AHB=0x%x\n",DRIVER_NAME,__func__, readl(emmc_port->emmc_membase+EMMC_AHB));
	return ret;
}

static int rtkemmc_resume(struct device *dev)
{
	int ret = 0;
	struct mmc_host *mmc = NULL;
	struct rtkemmc_host *emmc_port=NULL;
	struct mmc_host *host = NULL;

	mmc = dev_get_drvdata(dev);
	emmc_port = mmc_priv(mmc);
	if (!emmc_port)
		BUG();

	host = emmc_port->mmc;
	host->card->host = mmc;
	g_bResuming=1;

	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_AHB)|0x4,emmc_port->emmc_membase+EMMC_AHB);
	printk(KERN_ERR "%s: EMMC_AHB=0x%x\n", __func__, readl(emmc_port->emmc_membase+EMMC_AHB));

	if (!ret)
		ret = pm_runtime_force_resume(dev);

	rtkemmc_set_pin_mux(emmc_port);
	rtkemmc_init(emmc_port);
	sync(emmc_port);

	g_bResuming=0;
	init_completion(emmc_port->int_waiting);

	if(mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_resume(mmc);
		if (ret)
			pr_err("%s: cqe resume failed\n", __func__);
	}
	printk(KERN_ERR "[%s] Exit %s\n",DRIVER_NAME,__func__);

	return ret;
}

static const struct dev_pm_ops rtk_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rtkemmc_suspend, rtkemmc_resume)
};
#endif

void rtkemmc_dump_register(struct rtkemmc_host *emmc_port)
{
	emmc_port->backreg.sdmasa_r = readl(emmc_port->emmc_membase + EMMC_SDMASA_R);
	emmc_port->backreg.blocksize_r = readw(emmc_port->emmc_membase + EMMC_BLOCKSIZE_R);
	emmc_port->backreg.blockcount_r = readw(emmc_port->emmc_membase + EMMC_BLOCKCOUNT_R);
	emmc_port->backreg.xfer_mode_r = readw(emmc_port->emmc_membase + EMMC_XFER_MODE_R);
	emmc_port->backreg.host_ctrl1_r = readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
	emmc_port->backreg.pwr_ctrl_r = readb(emmc_port->emmc_membase + EMMC_PWR_CTRL_R);
	emmc_port->backreg.bgap_ctrl_r = readb(emmc_port->emmc_membase + EMMC_BGAP_CTRL_R);
	emmc_port->backreg.clk_ctrl_r = readw(emmc_port->emmc_membase + EMMC_CLK_CTRL_R);
	emmc_port->backreg.tout_ctrl_r = readb(emmc_port->emmc_membase + EMMC_TOUT_CTRL_R);
	emmc_port->backreg.normal_int_stat_en_r = readw(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_EN_R);
	emmc_port->backreg.error_int_stat_en_r = readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_EN_R);
	emmc_port->backreg.normal_int_signal_en_r = readw(emmc_port->emmc_membase + EMMC_NORMAL_INT_SIGNAL_EN_R);
	emmc_port->backreg.error_int_signal_en_r = readw(emmc_port->emmc_membase + EMMC_ERROR_INT_SIGNAL_EN_R);
	emmc_port->backreg.auto_cmd_stat_r = readw(emmc_port->emmc_membase + EMMC_AUTO_CMD_STAT_R);
	emmc_port->backreg.host_ctrl2_r = readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
	emmc_port->backreg.adma_sa_low_r = readl(emmc_port->emmc_membase + EMMC_ADMA_SA_LOW_R);
	emmc_port->backreg.mshc_ctrl_r = readb(emmc_port->emmc_membase + EMMC_MSHC_CTRL_R);
	emmc_port->backreg.ctrl_r = readb(emmc_port->emmc_membase + EMMC_CTRL_R);
	emmc_port->backreg.other1 = readl(emmc_port->emmc_membase + EMMC_OTHER1);
	emmc_port->backreg.dummy_sys = readl(emmc_port->emmc_membase + EMMC_DUMMY_SYS);
	emmc_port->backreg.dqs_ctrl1 = readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1);
	emmc_port->backreg.wcmd_ctrl = readl(emmc_port->emmc_membase + EMMC_WCMD_CTRL);

	emmc_port->backreg.rdq_ctrl0 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL0);
	emmc_port->backreg.rdq_ctrl1 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL1);
	emmc_port->backreg.rdq_ctrl2 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL2);
	emmc_port->backreg.rdq_ctrl3 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL3);
	emmc_port->backreg.rdq_ctrl4 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL4);
	emmc_port->backreg.rdq_ctrl5 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL5);
	emmc_port->backreg.rdq_ctrl6 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL6);
	emmc_port->backreg.rdq_ctrl7 = readl(emmc_port->emmc_membase+EMMC_RDQ_CTRL7);
	emmc_port->backreg.dq_ctrl_set = readl(emmc_port->emmc_membase+EMMC_DQ_CTRL_SET);
	emmc_port->backreg.ahb = readl(emmc_port->emmc_membase+EMMC_AHB);
}

void rtkemmc_restore_register(struct rtkemmc_host *emmc_port)
{
	rtkemmc_writel(emmc_port->backreg.sdmasa_r,emmc_port->emmc_membase + EMMC_SDMASA_R);
	rtkemmc_writew(emmc_port->backreg.blocksize_r,emmc_port->emmc_membase + EMMC_BLOCKSIZE_R);
	rtkemmc_writew(emmc_port->backreg.blockcount_r,emmc_port->emmc_membase + EMMC_BLOCKCOUNT_R);
	rtkemmc_writew(emmc_port->backreg.xfer_mode_r,emmc_port->emmc_membase + EMMC_XFER_MODE_R);
	rtkemmc_writeb(emmc_port->backreg.host_ctrl1_r,emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
	rtkemmc_writeb(emmc_port->backreg.pwr_ctrl_r,emmc_port->emmc_membase + EMMC_PWR_CTRL_R);
	rtkemmc_writeb(emmc_port->backreg.bgap_ctrl_r,emmc_port->emmc_membase + EMMC_BGAP_CTRL_R);
	rtkemmc_writew(emmc_port->backreg.clk_ctrl_r,emmc_port->emmc_membase + EMMC_CLK_CTRL_R);
	rtkemmc_writeb(emmc_port->backreg.tout_ctrl_r,emmc_port->emmc_membase + EMMC_TOUT_CTRL_R);
	rtkemmc_writew(emmc_port->backreg.normal_int_stat_en_r,emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_EN_R);
	rtkemmc_writew(emmc_port->backreg.error_int_stat_en_r,emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_EN_R);
	rtkemmc_writew(emmc_port->backreg.normal_int_signal_en_r,emmc_port->emmc_membase + EMMC_NORMAL_INT_SIGNAL_EN_R);
	rtkemmc_writew(emmc_port->backreg.error_int_signal_en_r,emmc_port->emmc_membase + EMMC_ERROR_INT_SIGNAL_EN_R);
	rtkemmc_writew(emmc_port->backreg.auto_cmd_stat_r,emmc_port->emmc_membase + EMMC_AUTO_CMD_STAT_R);
	rtkemmc_writew(emmc_port->backreg.host_ctrl2_r,emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
	rtkemmc_writel(emmc_port->backreg.adma_sa_low_r,emmc_port->emmc_membase + EMMC_ADMA_SA_LOW_R);
	rtkemmc_writeb(emmc_port->backreg.mshc_ctrl_r,emmc_port->emmc_membase + EMMC_MSHC_CTRL_R);
	rtkemmc_writeb(emmc_port->backreg.ctrl_r,emmc_port->emmc_membase + EMMC_CTRL_R);
	rtkemmc_writel(emmc_port->backreg.dummy_sys,emmc_port->emmc_membase + EMMC_DUMMY_SYS);

	rtkemmc_dqs_delay_tap(emmc_port, emmc_port->backreg.dqs_ctrl1);
	cmd_delay_tap_setting(emmc_port, emmc_port->backreg.wcmd_ctrl);

	if((emmc_port->backreg.rdq_ctrl0&0x80)!=0) {
		data_delay_tap_setting(emmc_port);
	}

	rtkemmc_writel(emmc_port->backreg.ahb, emmc_port->emmc_membase+EMMC_AHB);
}

void rtkemmc_restore_l4_register(struct rtkemmc_host *emmc_port)
{
	rtkemmc_writel(emmc_port->backreg.other1,emmc_port->emmc_membase + EMMC_OTHER1);
}

void error_handling(struct rtkemmc_host *emmc_port)
{
	int retry_cnt=0;
	int err=0;

	if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
		(EMMC_AUTO_CMD_ERR|EMMC_CMD_IDX_ERR|EMMC_CMD_END_BIT_ERR|EMMC_CMD_CRC_ERR|EMMC_CMD_TOUT_ERR)) !=0){ //check cmd line
#ifdef RTKEMMC_DEBUG
		printk(KERN_INFO "CMD Line error occurs \n");
#endif
		rtkemmc_writeb(0x2, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
		wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x2<<24), 0x0, __func__);	//wait for clear 0x2f bit 1
	}
	if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
		(EMMC_ADMA_ERR|EMMC_DATA_END_BIT_ERR|EMMC_DATA_CRC_ERR|EMMC_DATA_TOUT_ERR)) !=0){ //check data line
#ifdef RTKEMMC_DEBUG
		printk(KERN_INFO "DAT Line error occurs \n");
#endif
		rtkemmc_writeb(0x4, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
		wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x4<<24), 0x0, __func__); //wait for clear 0x2f bit 2
	}

retry_L1:
	rtkemmc_writew(readw(emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_R)&0xffff, emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_R);

	//synchronous abort: stop host dma
	rtkemmc_writeb(0x1, emmc_port->emmc_membase + EMMC_BGAP_CTRL_R); //stop emmc read/write transfer
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R), 0x2, 0x2, __func__); //wait for xfer complete
	rtkemmc_writew(0x2, emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_R); //clear transfer complete status

	isb();
	sync(emmc_port);
retry_L2:
	/*from eMMC Spec. stop command cannot be fired after the cmd 21*/
	if(emmc_port->cmd_opcode!=MMC_SEND_TUNING_BLOCK_HS200) {
		rtkemmc_stop_transmission(emmc_port->mmc->card, 1);
		mdelay(1);
	}

	err = rtkemmc_wait_status(emmc_port->mmc->card,STATE_TRAN,0,1);

	if(err) {
		if(err == -9999) {
			if((retry_cnt++)<10000) goto retry_L2;
			else {
				printk(KERN_ERR "%s: status check retry again because of not in trans state after cmd13!!!\n", __func__);
				retry_cnt = 0;
				goto retry_L2;
			}
		}
		else {
			if((retry_cnt++)<30) goto retry_L2;
			else {
				printk(KERN_ERR "%s: this phase is not recommanded, ret=%d\n", __func__, err);
			}
		}
	}

	rtkemmc_writeb(0x6, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x6<<24), 0x0, __func__); //wait for clear 0x2f bit 1 & 2

	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + EMMC_PSTATE_REG), 0x3, 0x0, __func__); //wait for cmd and data lines are not in use

	udelay(40);
	if((readl(emmc_port->emmc_membase + EMMC_PSTATE_REG) &0xf00000)!=0xf00000 || (readl(emmc_port->emmc_membase + EMMC_PSTATE_REG) & 0xf0)!=0xf0) {
#ifdef RTKEMMC_PHASE_TRACE
		printk(KERN_INFO "wait for data line signal..., EMMC_PSTATE_REG=0x%x\n", readl(emmc_port->emmc_membase + EMMC_PSTATE_REG));
#endif
		goto retry_L1;
	}
}

static int rtkemmc_send_cmd35(struct rtkemmc_host *emmc_port, u16 * state)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err=0;
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	cmd.opcode         = MMC_ERASE_GROUP_START;
	cmd.arg            = 0x00020000;
	cmd_info.cmd       = &cmd;
	cmd_info.emmc_port = emmc_port;
	cmd_info.rsp_len   = 6;

	err = SD_SendCMDGetRSP(&cmd_info,1);

	if(err ) {
		mmcmsg3(KERN_WARNING "%s: cmd 35 fail\n",DRIVER_NAME);
#ifdef RTKEMMC_PHASE_TRACE
		trace_err_status=emmc_port->error_interrupt;
		trace_normal_status=emmc_port->normal_interrupt;
		trace_auto_err_status=emmc_port->auto_error_interrupt;
#endif
		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_AUTO_CMD_ERR|EMMC_CMD_IDX_ERR|EMMC_CMD_END_BIT_ERR|EMMC_CMD_CRC_ERR|EMMC_CMD_TOUT_ERR))!=0){ //check cmd line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "CMD Line error occurs \n");
#endif
			rtkemmc_writeb(0x2, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x2<<24), 0x0, __func__); //wait for clear 0x2f bit 1
		}
		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_ADMA_ERR|EMMC_DATA_END_BIT_ERR|EMMC_DATA_CRC_ERR|EMMC_DATA_TOUT_ERR)) !=0){ //check data line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "DAT Line error occurs \n");
#endif
			rtkemmc_writeb(0x4, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x4<<24), 0x0, __func__); //wait for clear 0x2f bit 2
		}
	}
	else {
		u8 cur_state = R1_CURRENT_STATE(cmd.resp[0]);
		*state = cur_state;
		mmcmsg1("cur_state=%s\n",state_tlb[cur_state]);
	}

	return err;
}

static int rtkemmc_send_cmd13(struct rtkemmc_host *emmc_port, u16 * state)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err=0;
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	cmd.opcode         = MMC_SEND_STATUS;
	cmd.arg            = (1<<RCA_SHIFTER);
	cmd_info.cmd       = &cmd;
	cmd_info.emmc_port = emmc_port;
	cmd_info.rsp_len   = 6;

	err = SD_SendCMDGetRSP(&cmd_info,1);

	if(err ) {
		mmcmsg3(KERN_WARNING "%s: MMC_SEND_STATUS fail\n",DRIVER_NAME);
#ifdef RTKEMMC_PHASE_TRACE
		trace_err_status=emmc_port->error_interrupt;
		trace_normal_status=emmc_port->normal_interrupt;
		trace_auto_err_status=emmc_port->auto_error_interrupt;
#endif

		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_AUTO_CMD_ERR|EMMC_CMD_IDX_ERR|EMMC_CMD_END_BIT_ERR|EMMC_CMD_CRC_ERR|EMMC_CMD_TOUT_ERR))!=0){ //check cmd line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "CMD Line error occurs \n");
#endif
			rtkemmc_writeb(0x2, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x2<<24), 0x0, __func__); //wait for clear 0x2f bit 1
		}
		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_ADMA_ERR|EMMC_DATA_END_BIT_ERR|EMMC_DATA_CRC_ERR|EMMC_DATA_TOUT_ERR)) !=0){ //check data line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "DAT Line error occurs \n");
#endif
			rtkemmc_writeb(0x4, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x4<<24), 0x0, __func__); //wait for clear 0x2f bit 2
		}
	}
	else {
		u8 cur_state = R1_CURRENT_STATE(cmd.resp[0]);
		*state = cur_state;
		mmcmsg1("cur_state=%s\n",state_tlb[cur_state]);
	}

	return err;
}

int rtkemmc_send_cmd6(struct rtkemmc_host *emmc_port, u32 args, u16 * state, int phase)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err=0;
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	cmd.opcode         = MMC_SWITCH;
	cmd.arg            = args;
	cmd.flags	   = MMC_CMD_AC|MMC_RSP_SPI_R1B | MMC_RSP_R1B;
	cmd_info.cmd       = &cmd;
	cmd_info.emmc_port = emmc_port;
	cmd_info.rsp_len   = 6;

	err = SD_SendCMDGetRSP(&cmd_info,1);

	if(err ) {
#ifdef RTKEMMC_DEBUG
                pr_err("%s error: 0x98012030=0x%x, 0x98012032=0x%x, phase=%d\n",
			__func__, emmc_port->normal_interrupt, emmc_port->error_interrupt, phase>>1);
#endif
#ifdef RTKEMMC_PHASE_TRACE
		trace_cur_tuning_cmd = 6;
		printk(KERN_ERR "%s: trace_TX_window=0x%x, trace_RX_window=0x%x, trace_TX1_window=0x%x, trace_dqs_counter=%d\n",
			__func__, trace_TX_window, trace_RX_window, trace_TX1_window, trace_dqs_counter);
#endif
#ifdef RTKEMMC_PHASE_TRACE
		trace_err_status=emmc_port->error_interrupt;
		trace_normal_status=emmc_port->normal_interrupt;
		trace_auto_err_status=emmc_port->auto_error_interrupt;
#endif
		error_handling(emmc_port);
	}
	else {
		u8 cur_state = R1_CURRENT_STATE(cmd.resp[0]);
		*state = cur_state;
		mmcmsg1("cur_state=%s\n",state_tlb[cur_state]);
	}

	return err;
}

int rtkemmc_set_blocklen(struct mmc_card* card, unsigned int blocklen)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err=0;

	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	if (mmc_card_blockaddr(card) || mmc_card_ddr52(card) ||
		mmc_card_hs400(card) || mmc_card_hs400es(card))
		return 0;

	cmd.opcode = MMC_SET_BLOCKLEN;
	cmd.arg = blocklen;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	cmd_info.cmd       = &cmd;
	cmd_info.emmc_port = mmc_priv(card->host);
	cmd_info.rsp_len   = 6;

	err = SD_SendCMDGetRSP(&cmd_info, 0);

	if(err) {
		pr_err("%s error: 0x98012030=0x%x, 0x98012032=0x%x\n",
			__func__, cmd_info.emmc_port->normal_interrupt, cmd_info.emmc_port->error_interrupt);
	}

	return err;
}

int rtkemmc_switch(struct rtkemmc_host *emmc_port, u8 set, u8 index, u8 value, unsigned int timeout_ms)
{
        struct mmc_command cmd;
        struct sd_cmd_pkt cmd_info;
        int err=0;
        memset(&cmd, 0, sizeof(struct mmc_command));
        memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

        cmd.opcode         = MMC_SWITCH;
        cmd.arg            = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
                                (index << 16) |
                                (value << 8) |
                                set;
        cmd.flags          = MMC_CMD_AC|MMC_RSP_SPI_R1B | MMC_RSP_R1B;
        cmd_info.cmd       = &cmd;
        cmd_info.emmc_port = emmc_port;
        cmd_info.rsp_len   = 6;

        err = SD_SendCMDGetRSP(&cmd_info, 0);

        if(err ) {
                pr_err("%s error: 0x98012030=0x%x, 0x98012032=0x%x\n",
                        __func__, emmc_port->normal_interrupt, emmc_port->error_interrupt);
                error_handling(emmc_port);
        }

        return err;
}

int rtkemmc_send_cmd18(struct rtkemmc_host *emmc_port, int size, unsigned long addr, unsigned int bIgnore)
{
	int ret_err=0;
	struct sd_cmd_pkt cmd_info;
	//struct mmc_host *host = emmc_port->mmc;
	unsigned char *crd_tmp_buffer=NULL;
	struct mmc_data *data=NULL;
	struct mmc_command *cmd=NULL;
	int i=0;
	memset(&cmd_info, 0x00, sizeof(struct sd_cmd_pkt));

	crd_tmp_buffer = (unsigned char *)emmc_port->dma_paddr;
	if (crd_tmp_buffer == NULL) {
		pr_err("%s,%s : crd_tmp_buffer == NULL\n",DRIVER_NAME,__func__);
		return -5;
	}

	for(i=0;i<(size/4);i++) {
		*(u32 *)(emmc_port->dma_vaddr+(i*4)) = 0xdeadbeef;
		//isb();
		//sync(emmc_port);
	}
	wmb();

	if (cmd_info.cmd == NULL) {
		cmd  = (struct mmc_command*) kmalloc(sizeof(struct mmc_command),GFP_KERNEL);
		memset(cmd, 0x00, sizeof(struct mmc_command));
		cmd_info.cmd  = (struct mmc_command*) cmd;
	}
	cmd_info.emmc_port = emmc_port;
	cmd_info.cmd->arg = addr;
	cmd_info.cmd->opcode = MMC_READ_MULTIPLE_BLOCK;
	cmd_info.rsp_len         = 6;
	cmd_info.byte_count  = 0x200;
	cmd_info.block_count = size/cmd_info.byte_count;
	cmd_info.dma_buffer = crd_tmp_buffer;

	if (cmd_info.cmd->data == NULL) {
		data  = (struct mmc_data*) kmalloc(sizeof(struct mmc_data),GFP_KERNEL);
		memset(data, 0x00, sizeof(struct mmc_data));
		cmd_info.cmd->data = data;
		data->flags = MMC_DATA_READ;
	}
	else
		cmd_info.cmd->data->flags = MMC_DATA_READ;
	MMCPRINTF("\n*** %s %s %d, cmdidx=0x%02x(%d), resp_type=0x%08x, host=0x%08x, card=0x%08x -------\n",
		__FILE__, __func__, __LINE__, cmd_info.cmd->opcode, cmd_info.cmd->opcode, cmd_info.cmd->flags, host, host->card);
	ret_err = SD_Stream(&cmd_info, bIgnore);
	if (ret_err) {
#ifdef RTKEMMC_DEBUG
		printk(KERN_ERR "%s error: 0x98012030=0x%x, 0x98012032=0x%x, cmd->arg=0x%x, block count=%d\n",
			__func__, emmc_port->normal_interrupt, emmc_port->error_interrupt, addr, cmd_info.block_count);
#endif
#ifdef RTKEMMC_PHASE_TRACE
		trace_err_status=emmc_port->error_interrupt;
		trace_normal_status=emmc_port->normal_interrupt;
		trace_auto_err_status=emmc_port->auto_error_interrupt;
#endif
		error_handling(emmc_port);
        }

	if (cmd) {
		kfree(cmd);
		cmd_info.cmd = NULL;
		cmd=NULL;
	}
	if (data) {
		kfree(data);
		//cmd_info.cmd->data = NULL;
		data=NULL;
	}
	return ret_err;
}

int rtkemmc_send_cmd25(struct rtkemmc_host *emmc_port,int size, unsigned long addr, int data_src, int *hs400_data, unsigned int bIgnore)
{
        int ret_err=0,i=0;
        struct sd_cmd_pkt cmd_info;
//        struct mmc_host *host = emmc_port->mmc;
        char *crd_tmp_buffer=NULL;
        struct mmc_data *data=NULL;
        struct mmc_command *cmd=NULL;
//      unsigned long flags=0;
        memset(&cmd_info, 0x00, sizeof(struct sd_cmd_pkt));

        crd_tmp_buffer = (unsigned char *) emmc_port->dma_paddr;
        if (crd_tmp_buffer == NULL)
        {
                pr_err("%s,%s : crd_ext_csd == NULL\n",DRIVER_NAME,__func__);
                return -5;
        }

	if(data_src==0) {
		for(i=0;i<(size/4);i++)
		{
			if(GLOBAL==0x80000000) GLOBAL=0;
			else GLOBAL++;
			*(u32 *)(emmc_port->dma_vaddr+(i*4)) = GLOBAL;
		}
	}
	else if(data_src==1) {
		for(i=0;i<(size/4);i++)
                {
                        if(GLOBAL==0x80000000) GLOBAL=0;
                        else GLOBAL++;
                        *(u32 *)(emmc_port->dma_vaddr+(i*4)) = hs400_data[i];
                }
	}
	else pr_err("data_source flag should be 0 or 1\n");

        wmb();

        if (cmd_info.cmd == NULL)
        {
                cmd  = (struct mmc_command*) kmalloc(sizeof(struct mmc_command),GFP_KERNEL);
                memset(cmd, 0x00, sizeof(struct mmc_command));
                cmd_info.cmd  = (struct mmc_command*) cmd;
        }

        cmd_info.emmc_port = emmc_port;
        cmd_info.cmd->arg = addr;
        cmd_info.cmd->opcode = MMC_WRITE_MULTIPLE_BLOCK;
        cmd_info.rsp_len         = 6;
        cmd_info.byte_count  = 0x200;
        cmd_info.block_count = size/cmd_info.byte_count;
        cmd_info.dma_buffer = crd_tmp_buffer;

	if (cmd_info.cmd->data == NULL)
        {
                data  = (struct mmc_data*) kmalloc(sizeof(struct mmc_data),GFP_KERNEL);
                memset(data, 0x00, sizeof(struct mmc_data));
                cmd_info.cmd->data = data;
                data->flags = MMC_DATA_WRITE;
        }
        else
                cmd_info.cmd->data->flags = MMC_DATA_WRITE;

        MMCPRINTF("\n*** %s %s %d, cmdidx=0x%02x(%d), resp_type=0x%08x, host=0x%08x, card=0x%08x , cmd=0x%08x, data=0x%08x-------\n",
                __FILE__, __func__, __LINE__, cmd_info.cmd->opcode, cmd_info.cmd->opcode, cmd_info.cmd->flags, host, host->card,cmd,data);
        ret_err = SD_Stream(&cmd_info, bIgnore);
        if (ret_err)
        {
#ifdef RTKEMMC_DEBUG
		pr_err("%s error: 0x98012030=0x%x, 0x98012032=0x%x, cmd->arg=0x%x, block count=%d\n",
			__func__, emmc_port->normal_interrupt, emmc_port->error_interrupt, addr, cmd_info.block_count);
#endif
#ifdef RTKEMMC_PHASE_TRACE
		trace_err_status=emmc_port->error_interrupt;
		trace_normal_status=emmc_port->normal_interrupt;
		trace_auto_err_status=emmc_port->auto_error_interrupt;
#endif
		error_handling(emmc_port);
        }
        MMCPRINTF("\n*** %s %s %d, cmdidx=0x%02x(%d), resp_type=0x%08x, host=0x%08x, card=0x%08x , cmd=0x%08x, data=0x%08x-------\n",
                __FILE__, __func__, __LINE__, cmd_info.cmd->opcode, cmd_info.cmd->opcode, cmd_info.cmd->flags, host, host->card,cmd,data);
#if 1
        if (cmd)
        {
                cmd_info.cmd = NULL;
                kfree(cmd);
                cmd=NULL;
        }
        if (data)
        {
                //cmd_info.cmd->data = NULL;
                kfree(data);
                data=NULL;
        }
#endif
        sync(emmc_port);
        return ret_err;
}

int rtkemmc_send_cmd21(struct rtkemmc_host *emmc_port, int size, unsigned long addr)
{
        int ret_err=0;
        struct sd_cmd_pkt cmd_info;
        //struct mmc_host *host = emmc_port->mmc;
        unsigned char *crd_tmp_buffer=NULL;
        struct mmc_data *data=NULL;
        struct mmc_command *cmd=NULL;
        int i=0;
        memset(&cmd_info, 0x00, sizeof(struct sd_cmd_pkt));

        crd_tmp_buffer = (unsigned char *)emmc_port->dma_paddr;
        if (crd_tmp_buffer == NULL) {
                pr_err("%s,%s : crd_tmp_buffer == NULL\n",DRIVER_NAME,__func__);
                return -5;
        }

        for(i=0;i<(size/4);i++) {
                *(u32 *)(emmc_port->dma_vaddr+(i*4)) = 0xdeadbeef;
                //isb();
                //sync(emmc_port);
        }
        wmb();

        if (cmd_info.cmd == NULL) {
                cmd  = (struct mmc_command*) kmalloc(sizeof(struct mmc_command),GFP_KERNEL);
                memset(cmd, 0x00, sizeof(struct mmc_command));
                cmd_info.cmd  = (struct mmc_command*) cmd;
        }
        cmd_info.emmc_port = emmc_port;
        cmd_info.cmd->arg = addr;
        cmd_info.cmd->opcode = MMC_SEND_TUNING_BLOCK_HS200;
        cmd_info.rsp_len         = 6;
        cmd_info.byte_count  = 0x80;
        cmd_info.block_count = 1;
        cmd_info.dma_buffer = crd_tmp_buffer;

        if (cmd_info.cmd->data == NULL) {
                data  = (struct mmc_data*) kmalloc(sizeof(struct mmc_data),GFP_KERNEL);
                memset(data, 0x00, sizeof(struct mmc_data));
                cmd_info.cmd->data = data;
                data->flags = MMC_DATA_READ;
        }
        else
                cmd_info.cmd->data->flags = MMC_DATA_READ;
        MMCPRINTF("\n*** %s %s %d, cmdidx=0x%02x(%d), resp_type=0x%08x, host=0x%08x, card=0x%08x -------\n",
                __FILE__, __func__, __LINE__, cmd_info.cmd->opcode, cmd_info.cmd->opcode, cmd_info.cmd->flags, host, host->card);
        ret_err = SD_Stream(&cmd_info, 1);
	if (ret_err) {
#ifdef RTKEMMC_DEBUG
		printk(KERN_INFO "Tuning rx cmd 21 err and call error handling\n");
#endif
		error_handling(emmc_port);
        }

        if (cmd) {
                kfree(cmd);
                cmd_info.cmd = NULL;
                cmd=NULL;
        }
        if (data) {
                kfree(data);
                //cmd_info.cmd->data = NULL;
                data=NULL;
        }
        return ret_err;
}

int rtkemmc_write_protect_cmd(struct rtkemmc_host *emmc_port, u32 args, bool is_wrtie_protect)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err=0;
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	if(is_wrtie_protect)
		cmd.opcode         = MMC_SET_WRITE_PROT;
	else
		cmd.opcode         = MMC_CLR_WRITE_PROT;

	cmd.arg            = args;
	cmd.flags          = MMC_CMD_AC|MMC_RSP_SPI_R1B | MMC_RSP_R1B;
	cmd_info.cmd       = &cmd;
	cmd_info.emmc_port = emmc_port;
	cmd_info.rsp_len   = 6;

	err = SD_SendCMDGetRSP(&cmd_info,1);

	if(err ) {
		pr_err("%s error: 0x98012030=0x%x, 0x98012032=0x%x\n",
			__func__, emmc_port->normal_interrupt, emmc_port->error_interrupt);

		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_AUTO_CMD_ERR|EMMC_CMD_IDX_ERR|EMMC_CMD_END_BIT_ERR|EMMC_CMD_CRC_ERR|EMMC_CMD_TOUT_ERR))!=0){ //check cmd line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "CMD Line error occurs \n");
#endif
			rtkemmc_writeb(0x2, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x2<<24), 0x0, __func__); //wait for clear 0x2f bit 1
		}
		if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
			(EMMC_ADMA_ERR|EMMC_DATA_END_BIT_ERR|EMMC_DATA_CRC_ERR|EMMC_DATA_TOUT_ERR)) !=0){ //check data line
#ifdef RTKEMMC_DEBUG
			printk(KERN_INFO "DAT Line error occurs \n");
#endif
			rtkemmc_writeb(0x4, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x4<<24), 0x0, __func__); //wait for clear 0x2f bit 2
		}
	}

	mdelay(1);
	return err;
}

int rtkemmc_query_protect_cmd(struct rtkemmc_host *emmc_port, unsigned long addr, u32 cmd_idx)
{
	int ret_err=0;
	struct sd_cmd_pkt cmd_info;
	unsigned char *crd_tmp_buffer=NULL;
	struct mmc_data *data=NULL;
	struct mmc_command *cmd=NULL;
	int i=0;
	memset(&cmd_info, 0x00, sizeof(struct sd_cmd_pkt));

	crd_tmp_buffer = (unsigned char *)emmc_port->dma_paddr;
	if (crd_tmp_buffer == NULL) {
		pr_err(KERN_ERR "%s,%s : crd_tmp_buffer == NULL\n",DRIVER_NAME,__func__);
		return -5;
	}

	for(i=0;i<(8/4);i++) {
		*(u32 *)(emmc_port->dma_vaddr+(i*4)) = 0xdeadbeef;
	}
	wmb();

	if (cmd_info.cmd == NULL) {
		cmd  = (struct mmc_command*) kmalloc(sizeof(struct mmc_command),GFP_KERNEL);
		memset(cmd, 0x00, sizeof(struct mmc_command));
		cmd_info.cmd  = (struct mmc_command*) cmd;
	}
	cmd_info.emmc_port = emmc_port;
	cmd_info.cmd->arg = addr;
	cmd_info.cmd->opcode = cmd_idx;
	cmd_info.rsp_len         = 6;
	if(cmd_idx==MMC_SEND_WRITE_PROT)
		cmd_info.byte_count  = 0x4;
	else
		cmd_info.byte_count  = 0x8;
	cmd_info.block_count = 1;
	cmd_info.dma_buffer = crd_tmp_buffer;

	if (cmd_info.cmd->data == NULL) {
		data  = (struct mmc_data*) kmalloc(sizeof(struct mmc_data),GFP_KERNEL);
		memset(data, 0x00, sizeof(struct mmc_data));
		cmd_info.cmd->data = data;
		data->flags = MMC_DATA_READ;
	}
	else
		cmd_info.cmd->data->flags = MMC_DATA_READ;

	ret_err = SD_Stream(&cmd_info, 1);
	if (ret_err) {
#ifdef RTKEMMC_DEBUG
		printk(KERN_INFO "cmd %u err and call error handling\n", cmd_idx);
#endif
		error_handling(emmc_port);
	}
	if(cmd_idx==MMC_SEND_WRITE_PROT)
		printk(KERN_ERR "cmd30: from sector 0x%x = 0x%x\n", addr, *(u32 *)(emmc_port->dma_vaddr));
	else {
		printk(KERN_ERR "cmd31: from sector 0x%x = 0x%x\n", addr, *(u32 *)(emmc_port->dma_vaddr));
		printk(KERN_ERR "cmd31: from sector 0x%x = 0x%x\n", addr, *(u32 *)(emmc_port->dma_vaddr+4));
	}

	if (cmd) {
		kfree(cmd);
		cmd_info.cmd = NULL;
		cmd=NULL;
	}
	if (data) {
		kfree(data);
		//cmd_info.cmd->data = NULL;
		data=NULL;
	}
	return ret_err;
}

int search_best(u32 window, u32 range)
{
        int i, j, k, max;
        int window_temp[32];
        int window_start[32];
        int window_end[32];
        int window_max=0;
        int window_best=0;
        int parse_end=1;

	if(window==0xffffffff){
		window_best=0x10;
		return window_best;
	}
	else if((window==0xffff)&&(range==0x10)){
		window_best=0x8;
		return window_best;
	}

        for( i=0; i<0x20; i++ ) {
                window_temp[i]=0;
                window_start[i]=0;
                window_end[i]=-1;
        }
        j=1;
        i=0;
        k=0;
        max=0;
	while((i<(range-1)) && (k<(range-1))){
                parse_end=0;
                for( i=window_end[j-1]+1; i<range; i++ ){
                        if (((window>>i)&1)==1 ){
                                window_start[j]=i;
                                break;
                        }
                }
                if( i==range){
                        break;
                }
                for( k=window_start[j]+1; k<range; k++ ){
                        if(((window>>k)&1)==0){
                                window_end[j]=k-1;
                                parse_end=1;
                                break;
                        }
                }
                if(parse_end==0){
                        window_end[j]=range-1;
                }
                j++;
        }
        for(i=1; i<j; i++){
                window_temp[i]= window_end[i]-window_start[i]+1;
        }
        if((((window)&1)==1)&&(((window>>(range-1))&1)==1))
        {
                window_temp[1]=window_temp[1]+window_temp[j-1];
                window_start[1]=window_start[j-1];
        }
        for(i=1; i<j; i++){
                if(window_temp[i]>window_max){
                        window_max=window_temp[i];
                        max=i;
                }
        }

        if((((window&1)==1)&&(((window>>(range-1))&1)==1))&&(max==1)){
		window_best=(((window_start[max]+window_end[max]+range)/2)&(range-1));
        }
        else {
                window_best=((window_start[max]+window_end[max])/2)&0x1f;
        }

	if(window_max>4){
		return window_best;
	}

	return 0xff;
}

int rtkemmc_phase_tuning(struct rtkemmc_host *emmc_port,u32 mode,int flag, int stage)
{
	u32 TX_window=0;
        u32 RX_window=0;
	u32 TX1_window=0;
	int TX_best=0x0;
	int RX_best=0x0;
	int TX1_best=0x0;
        int i=0;
        u32 range=0;
	u32 rx_range=0x20;
        u16 state=0;
	unsigned int loop_cnt=0;

        if (mode == MODE_HS400 || mode == MODE_DDR)
                range = 0x10;
        else
		range = 0x20;

	if (mode == MODE_HS400)
		loop_cnt=0;
	else if(mode == MODE_HS200)
		loop_cnt=20;
	else
		loop_cnt=30;

	if(emmc_port->suspend == 1 && (emmc_port->tx_tuning || emmc_port->rx_tuning) && emmc_port->mmc->card->cid.manfid != 0x13)
	{
		if(emmc_port->tx_tuning==0 && emmc_port->rx_tuning==1) {
			phase(emmc_port, emmc_port->tx_phase, suspend_VP1);
		}
		else if(emmc_port->tx_tuning==1 && emmc_port->rx_tuning==0)
		{
			phase(emmc_port, suspend_VP0, emmc_port->rx_phase);
		}
		else {
			phase(emmc_port, suspend_VP0, suspend_VP1);
		}

		rtkemmc_reset_fifo(emmc_port);

		printk(KERN_ERR "suspend/resume: restore tx & rx phase: TX=0x%x, RX=0x%x\n", suspend_VP0, suspend_VP1);
		if(mode == MODE_HS200) {
			emmc_port->suspend = 0;
		}
		return 0;
	}
#ifdef PHASE_INHERITED
	if (emmc_port->tx_tuning || emmc_port->rx_tuning){
                phase(emmc_port, (emmc_port->tx_tuning)?0xff:emmc_port->tx_phase, (emmc_port->rx_tuning)?0xff:emmc_port->rx_phase);

		rtkemmc_reset_fifo(emmc_port);
        }
        else {
                phase(emmc_port, emmc_port->tx_phase, emmc_port->rx_phase); //VP0, VP1 phase

		rtkemmc_reset_fifo(emmc_port);

		printk(KERN_INFO "Inherit bootcode tuning phase: TX=0x%x, RX=0x%x\n", emmc_port->tx_phase, emmc_port->rx_phase);
                return 0;
        }
#else
	phase(emmc_port, 0, 0); //VP0, VP1 phase

	rtkemmc_reset_fifo(emmc_port);

	mdelay(5);
        sync(emmc_port);
#endif

	/*Actually, in Stark, the clock source is from crc, command tx does not need to be tuned, but this action is for safety.
	We ecpected that tx phase is 0xffffffff(hs200) or 0xffff(hs400) if the clock source is from crc
	if user encounter a bad IC, they can adjust the clock source from 0x98012420 [14:15] 2b' 00 to 2b' 10,
	Again, this tx tuning is needed*/
	if((soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)) &&
		(readw(emmc_port->emmc_membase+EMMC_OTHER1)&(0x3<<14))==0)
	{
		if(mode==MODE_HS400 || mode == MODE_DDR) {
			TX_window = 0xffff;
		}
		else {
			TX_window = 0xffffffff;
		}
		suspend_VP0 = TX_best;
	}
	else if ((stage==TUNING_STAGE1 || stage==TUNING_STAGE_BOTH) && emmc_port->tx_tuning) {
		if (mode == MODE_DDR && flag==1)
			printk(KERN_ERR "Start DDR50 TX Tuning:\n");
		else if (mode == MODE_HS400 && flag==1)
			printk(KERN_ERR "Start HS400 TX Tuning: \n");
		else if(flag==1)
			printk(KERN_ERR "Start HS200 TX Tuning:\n");
                for(i=0;i<range;i++) {
#ifdef RTKEMMC_PHASE_TRACE
			trace_cur_tuning_cmd = 35;
			trace_index = i;
#endif
                        phase(emmc_port, i, 0xff);

			rtkemmc_reset_fifo(emmc_port);
#ifdef DEBUG
                        printk("phase =0x%x \n", i);
#endif
			rtkemmc_send_cmd35(emmc_port, &state);
			if((emmc_port->error_interrupt&0x01) != 0)
                                TX_window= TX_window&(~(1<<i));
                        else
                                TX_window= TX_window|((1<<i));
#ifdef RTKEMMC_PHASE_TRACE
			trace_phase_bitmap = TX_window;
#endif
                }
#ifdef RTKEMMC_PHASE_TRACE
		trace_TX_window = TX_window;
#endif
		TX_best = search_best(TX_window, range);
		if(flag==1)printk(KERN_ERR "TX_WINDOW = 0x%08x TX_best=0x%x\n", TX_window, TX_best);
                phase(emmc_port, TX_best, 0xff);

		rtkemmc_reset_fifo(emmc_port);

		suspend_VP0 = TX_best;

		if(TX_best==0xff) return -1;
        }

	if ((stage==TUNING_STAGE2 || stage==TUNING_STAGE_BOTH) && emmc_port->rx_tuning) {
		if (mode == MODE_DDR && flag==1)
			printk(KERN_ERR "Start DDR50 RX Tuning:\n");
		else if (mode == MODE_HS400 && flag==1)
			printk(KERN_ERR "Start HS400 RX Tuning:\n");
		else if(flag==1)
			printk(KERN_ERR "Start HS200 RX Tuning:\n ");

rx_retry:
                for(i=0;i<rx_range;i++) {
#ifdef RTKEMMC_PHASE_TRACE
			trace_index = i;
#endif
                        phase(emmc_port, 0xff, i);

			rtkemmc_reset_fifo(emmc_port);
#ifdef DEBUG
                        printk("phase =0x%x \n", i);
#endif
			if (mode == MODE_HS200) {
#ifdef RTKEMMC_PHASE_TRACE
				trace_cur_tuning_cmd = 21;
#endif
				if(rtkemmc_send_cmd21(emmc_port, 128, 0x0) != 0)
					RX_window= RX_window&(~(1<<i));
				else
					RX_window= RX_window|((1<<i));
			}
			else {
#ifdef RTKEMMC_PHASE_TRACE
				trace_cur_tuning_cmd = 13;
#endif
				if(rtkemmc_send_cmd13(emmc_port, &state) != 0)
                                        RX_window= RX_window&(~(1<<i));
                                else
                                        RX_window= RX_window|((1<<i));
			}
#ifdef RTKEMMC_PHASE_TRACE
                        trace_phase_bitmap = RX_window;
#endif
                }
#ifdef RTKEMMC_PHASE_TRACE
		trace_RX_window = RX_window;
#endif
		RX_best = search_best(RX_window, rx_range);

		if(RX_window==0xffffffff) {
			loop_cnt++;
			switch(loop_cnt) {
				case 10:
					rtkemmc_set_pad_driving(emmc_port, 3, 3, 3, 0);
					printk(KERN_ERR "try pad driving 3: RX_WINDOW = 0x%08x RX_best=0x%x\n", RX_window,RX_best);
					break;
				case 20:
					rtkemmc_set_pad_driving(emmc_port, 2, 2, 2, 0);
					printk(KERN_ERR "try pad driving 2: RX_WINDOW = 0x%08x RX_best=0x%x\n", RX_window,RX_best);
					break;
				case 30:
					rtkemmc_set_pad_driving(emmc_port, 1, 1, 1, 0);
					printk(KERN_ERR "try pad driving 1: RX_WINDOW = 0x%08x RX_best=0x%x\n", RX_window,RX_best);
					break;
				case 40:
					rtkemmc_set_pad_driving(emmc_port, 0, 0, 0, 0);
					printk(KERN_ERR "try pad driving 0: RX_WINDOW = 0x%08x RX_best=0x%x\n", RX_window,RX_best);
					break;
				default:
					if(loop_cnt>60)
						pr_err("loop cnt %d: RX_WINDOW = 0x%08x, cannot find a proper rx phase\n", loop_cnt, RX_window);
			}

			if(loop_cnt<=60)
				goto rx_retry;
		}
		else {
			rtkemmc_set_pad_driving(emmc_port,
						emmc_port->pddrive_nf[1],
						emmc_port->pddrive_nf[2],
						emmc_port->pddrive_nf[3],
						emmc_port->pddrive_nf[4]);
		}
		if(flag==1)printk(KERN_ERR "RX_WINDOW = 0x%08x RX_best=0x%x\n", RX_window,RX_best);

                phase(emmc_port, 0xff, RX_best);

		rtkemmc_reset_fifo(emmc_port);

		suspend_VP1 = RX_best;

		if(RX_best==0xff) return -1;
        }
#ifdef RTKEMMC_PHASE_TRACE
	trace_index = 1000;
#endif
	if (mode == MODE_HS400)
		loop_cnt=0;
	else
		loop_cnt=20;

	if ((stage==TUNING_STAGE2 || stage==TUNING_STAGE_BOTH) &&
		(mode == MODE_HS400 || mode == MODE_DDR ||
		 soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)) &&
		emmc_port->tx_tuning) {
		TX1_window= TX_window;
		if(flag==1 && mode == MODE_HS400) printk(KERN_ERR "Start HS400 TX Tuning2:\n");
		else if(flag==1 && mode == MODE_DDR) printk(KERN_ERR "Start DDR50 TX Tuning2:\n");
		else if(flag==1) printk(KERN_ERR "Start HS200 TX Tuning2:\n");

tx1_retry:
                for(i=0;i<range;i++) {
#ifdef RTKEMMC_PHASE_TRACE
			trace_cur_tuning_cmd = 25;
#endif
			if(((TX_window)&(1<<i))!=0) {
#ifdef RTKEMMC_PHASE_TRACE
				trace_index = i;
#endif
				phase(emmc_port, i, 0xff);

				rtkemmc_reset_fifo(emmc_port);
#ifdef DEBUG
				printk("phase =0x%x \n", i);
#endif
				if(rtkemmc_send_cmd25(emmc_port, 1024, (emmc_port->emmc_tuning_addr/512),0,NULL ,1) != 0)
					TX1_window= TX1_window&(~(1<<i));
			}
#ifdef RTKEMMC_PHASE_TRACE
                        trace_phase_bitmap = TX1_window;
#endif
		}
#ifdef RTKEMMC_PHASE_TRACE
		trace_TX1_window = TX1_window;
#endif
		TX1_best = search_best(TX1_window, range);
		if(((mode == MODE_HS400 || mode == MODE_DDR ) && TX1_window==0xffff) ||
			(mode == MODE_HS200 && TX1_window==0xffffffff)) {
			loop_cnt++;
			switch(loop_cnt) {
			case 10:
				rtkemmc_set_pad_driving(emmc_port, 3, 3, 3, 0);
				printk(KERN_ERR "try pad driving 3: TX1_WINDOW = 0x%08x TX1_best=0x%x\n", TX1_window,TX1_best);
				break;
			case 20:
				rtkemmc_set_pad_driving(emmc_port, 2, 2, 2, 0);
				printk(KERN_ERR "try pad driving 2: TX1_WINDOW = 0x%08x TX1_best=0x%x\n", TX1_window,TX1_best);
				break;
			case 30:
				rtkemmc_set_pad_driving(emmc_port, 1, 1, 1, 0);
				printk(KERN_ERR "try pad driving 1: TX1_WINDOW = 0x%08x TX1_best=0x%x\n", TX1_window,TX1_best);
				break;
			case 40:
				rtkemmc_set_pad_driving(emmc_port, 0, 0, 0, 0);
				printk(KERN_ERR "try pad driving 0: TX1_WINDOW = 0x%08x TX1_best=0x%x\n", TX1_window,TX1_best);
				break;
			default:
				if(loop_cnt>60)
					pr_err("loop cnt %d: TX1_WINDOW = 0x%08x, cannot find a proper tx1 phase\n", loop_cnt, TX1_window);
			}

			if(loop_cnt<=60)
				goto tx1_retry;
		}
		else {
			rtkemmc_set_pad_driving(emmc_port,
                                                emmc_port->pddrive_nf[1],
                                                emmc_port->pddrive_nf[2],
                                                emmc_port->pddrive_nf[3],
                                                emmc_port->pddrive_nf[4]);
		}

		if(flag==1)printk(KERN_ERR "TX1_WINDOW = 0x%08x TX1_best=0x%x\n", TX1_window,TX1_best);

                phase(emmc_port, TX1_best, 0xff);

		rtkemmc_reset_fifo(emmc_port);

		suspend_VP0 = TX1_best;

		if(TX1_best==0xff) return -1;
        }
	sync(emmc_port);

	return 0;
}

static int mmc_Tuning_DDR50(struct rtkemmc_host *emmc_port)
{
	if(emmc_port->retune==0)
		down_write(&emmc_port->cr_rw_sem);

	if (emmc_port->pddrive_nf[0] != 0 )
		rtkemmc_set_pad_driving(emmc_port, emmc_port->pddrive_nf[1], emmc_port->pddrive_nf[2], emmc_port->pddrive_nf[3], emmc_port->pddrive_nf[4]);
        else {
		rtkemmc_set_pad_driving(emmc_port,0x0, 0x0, 0x0, 0x0);
	}
	if(emmc_port->tx_tuning && emmc_port->rx_tuning) {	//device tree set kernel tuning for DDR50
		rtkemmc_phase_tuning(emmc_port, MODE_DDR,1, TUNING_STAGE_BOTH);
		sync(emmc_port);
		mdelay(10);
	}
	else {
		phase(emmc_port, 0x8, 0x9);

		rtkemmc_reset_fifo(emmc_port);
	}
	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&0xfffffffe, emmc_port->emmc_membase+EMMC_OTHER1);        //enable L4 gated after DDR50 finished

	emmc_port->time_setting = 0;

	if(emmc_port->retune==0)
		up_write(&emmc_port->cr_rw_sem);
	return 0;
}

static int mmc_Tuning_HS200(struct rtkemmc_host *emmc_port)
{
	MMCPRINTF("%s \n", __func__);

	if(emmc_port->retune==0)
		down_write(&emmc_port->cr_rw_sem);

	if (emmc_port->pddrive_nf[0] != 0)
		rtkemmc_set_pad_driving(emmc_port, emmc_port->pddrive_nf[1], emmc_port->pddrive_nf[2], emmc_port->pddrive_nf[3], emmc_port->pddrive_nf[4]);
	else {
		rtkemmc_set_pad_driving(emmc_port,0x2, 0x2, 0x2, 0x0);
	}

	rtkemmc_phase_tuning(emmc_port, MODE_HS200,1, TUNING_STAGE_BOTH);
	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&0xfffffffe, emmc_port->emmc_membase+EMMC_OTHER1);        //enable L4 gated after HS200 finished
	sync(emmc_port);
	mdelay(10);
	emmc_port->time_setting = 0;

	if(emmc_port->retune==0)
		up_write(&emmc_port->cr_rw_sem);

	printk(KERN_ERR "HS200: final phase=0x%x\n", readl(emmc_port->crt_membase + SYS_PLL_EMMC1));

	return 0;
}

static int mmc_Tuning_HS400(struct rtkemmc_host *emmc_port)
{
	if(emmc_port->retune==0)
		down_write(&emmc_port->cr_rw_sem);

	if (emmc_port->pddrive_nf[0] != 0)
		rtkemmc_set_pad_driving(emmc_port, emmc_port->pddrive_nf[1], emmc_port->pddrive_nf[2], emmc_port->pddrive_nf[3], emmc_port->pddrive_nf[4]);
	else {
		rtkemmc_set_pad_driving(emmc_port,0x4, 0x4, 0x4, 0x0);
	}
	rtkemmc_phase_tuning(emmc_port, MODE_HS400,1, TUNING_STAGE_BOTH);

	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&0xfffffffe, emmc_port->emmc_membase+EMMC_OTHER1);        //enable L4 gated after HS400 finished
	sync(emmc_port);
	mdelay(10);
	if(emmc_port->retune==0)
		up_write(&emmc_port->cr_rw_sem);
	printk(KERN_ERR "HS400 first stage: final phase=0x%x\n", readl(emmc_port->crt_membase + SYS_PLL_EMMC1));

	return 0;
}

static int rtkemmc_execute_tuning(struct mmc_host *host, u32 opcode)
{
	struct rtkemmc_host *emmc_port;
	struct sd_cmd_pkt cmd_info;
	MMCPRINTF("%s \n", __func__);

	emmc_port = mmc_priv(host);
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	cmd_info.emmc_port = emmc_port;

	if (host->card){
		printk(KERN_INFO "emmc card manid = 0x%08x\n", host->card->cid.manfid);
		if (host->card->cid.manfid == 0x13) //micron manfid
			emmc_port->rx_tuning = 1; //micron: force to turn on rx tuning
	}
	else
		pr_err("host->card is null! \n");

	g_bTuning = 1;


	switch(emmc_port->speed_step)
	{
	case 0:
		break;
	case 1:
		mmc_Tuning_DDR50(emmc_port);
		break;
	case 2:
		mmc_Tuning_HS200(emmc_port);
		break;
	case 3:
		mmc_Tuning_HS400(emmc_port);	//hs400 still have dqs tuning, so set flag as 1 after dqs tuning
		break;
	default:
		break;
	}

	g_bTuning = 0;
	return 0;
}

#ifdef SHA256
static int rtk_get_hash(unsigned char *input, unsigned char *sha256_hash, unsigned int dma_len)
{
	struct crypto_shash *alg = NULL;
	struct sdesc *sdesc = NULL;
	unsigned char output[32] = {0};
	char *hash_alg_name = "sha256";
	int ret = -1;
	int size = 0;

	alg = crypto_alloc_shash(hash_alg_name, 0, 0);
	if (IS_ERR(alg)) {
		pr_info("%s: can't alloc alg %s\n", __func__, hash_alg_name);
		ret = -1;
		goto exit;
	}

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (IS_ERR(sdesc)) {
		pr_err("%s: can't alloc sdesc\n", __func__);
		ret = -1;
		goto exit;
	}

	sdesc->shash.tfm = alg;

	ret = crypto_shash_digest(&sdesc->shash, input, dma_len, output);
	if (ret != 0) {
		pr_err("%s: can't get hash\n", __func__);
		ret = -1;
		goto exit;
	}

	memcpy(sha256_hash, output, 32);

	ret = 0;
exit:
	if(alg)
		crypto_free_shash(alg);
	if(sdesc)
		kfree(sdesc);
	return ret;
}
#endif

static int rw_test_tuning(struct rtkemmc_host *emmc_port,unsigned long emmc_blk_addr)
{
	int i;
#ifdef RTKEMMC_PHASE_TRACE
        trace_cur_tuning_cmd = 26;
#endif
	rtkemmc_send_cmd25(emmc_port,DMA_ALLOC_LENGTH, emmc_blk_addr,0, NULL, 1);
#ifdef SHA256
	rtk_get_hash(emmc_port->dma_vaddr, compare3, DMA_ALLOC_LENGTH);
#else
	memcpy(compare1, emmc_port->dma_vaddr, DMA_ALLOC_LENGTH);
#endif
#ifdef RTKEMMC_PHASE_TRACE
        trace_cur_tuning_cmd = 27;
#endif
	rtkemmc_send_cmd18(emmc_port,DMA_ALLOC_LENGTH, emmc_blk_addr, 1);
#ifdef SHA256
	rtk_get_hash(emmc_port->dma_vaddr, compare4, DMA_ALLOC_LENGTH);
#else
	memcpy(compare2, emmc_port->dma_vaddr, DMA_ALLOC_LENGTH);
#endif

#ifdef SHA256
	for(i=0;i<32;i++) {
		if(compare3[i]!=compare4[i]) {
			return 1;
		}
	}
#else
	for(i=0;i<DMA_ALLOC_LENGTH;i++) {
		if(compare1[i]!=compare2[i]) {
			return 1;
		}
	}
#endif
	return 0;
}

static void rtkemmc_dqs_tuning(struct mmc_host *host)
{
	int i=0, j=0, ret;
	struct rtkemmc_host *emmc_port;
	unsigned long dqs_tuning_blk_addr=0;
	int retry_count=0;
	unsigned int bitmap=0;
	unsigned int max=0;
	unsigned int retry_cmd_dly_tap = 0x0;
	int hs400_data[128]={0};        //4 bytes header, 33aa, 4 bytes for TX, 4 bytes for RX, 4 bytes for dqs
	unsigned int *buf;
	size_t buf_size;

        emmc_port = mmc_priv(host);
	mdelay(2);

	if(emmc_port->suspend == 1)
	{
		if(emmc_port->dqs_tuning == 1) {
			rtkemmc_dqs_delay_tap(emmc_port, suspend_dqs);
		}
		else {
			rtkemmc_dqs_delay_tap(emmc_port, emmc_port->dqs);
		}
                printk(KERN_ERR "suspend/resume: restore DQS=0x%x\n", readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1));
		emmc_port->suspend = 0;
		emmc_port->time_setting = 0;
		return;
	}
	if(emmc_port->retune==0)
		down_write(&emmc_port->cr_rw_sem);
	g_bTuning = 1;

#ifdef SHA256
	printk(KERN_ERR "[EMMC] SHA256 is enabled for dqs comparison!!!\n");
	if(!compare3)
		compare3 = dma_alloc_coherent(emmc_port->dev, 32, &compare3_phy_addr ,GFP_KERNEL);
	if(!compare4)
		compare4 = dma_alloc_coherent(emmc_port->dev, 32, &compare4_phy_addr ,GFP_KERNEL);
#endif

#ifdef DQS_INHERITED
        if(emmc_port->dqs_tuning == 0) {
		rtkemmc_dqs_delay_tap(emmc_port, emmc_port->dqs);

		printk(KERN_ERR "Inherit bootcode dqs: DQS=0x%x\n", readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1));
		printk(KERN_ERR "read/write test for inherit hs400 parameter...\n");
		if( rw_test_tuning(emmc_port, (emmc_port->emmc_tuning_addr/512))==0) {
			printk(KERN_ERR "read/write test success for hs400 parameter!!!\n");
			g_bTuning = 0;
			emmc_port->time_setting = 0;
			if(emmc_port->retune==0)
				up_write(&emmc_port->cr_rw_sem);

#ifdef SHA256
			if(compare3)
				dma_free_coherent(emmc_port->dev, 32, compare3 , compare3_phy_addr);
			if(compare4)
				dma_free_coherent(emmc_port->dev, 32, compare4 , compare4_phy_addr);
			compare3 = NULL;
			compare4 = NULL;
#endif
			return;
		}
		else {
			emmc_port->dqs_tuning=1;
			emmc_port->tx_tuning = 1;
			emmc_port->rx_tuning = 1;
			pr_err("read/write test failed, retune the hs400...\n");
		}
	}
#endif

	dqs_tuning_blk_addr = emmc_port->emmc_tuning_addr / 512;	//convert from offset to block address
	printk(KERN_ERR "emmc_port->emmc_tuning_addr = 0x%lx, dqs_tuning_blk_addr = 0x%lx\n", emmc_port->emmc_tuning_addr, dqs_tuning_blk_addr);

retry:
	bitmap=0;
	max=0;
	j=0;
	ret = -1;
#ifdef RTKEMMC_PHASE_TRACE
	trace_dqs_counter++;
#endif
	for(i=0; i<0x20; i++) {
		if(j>=5)			//must be more than 5 continuous tap sample point
			max = j;
		if(j==0 && max!=0)	//find the max tap length
			break;
#ifdef RTKEMMC_DEBUG
		printk(KERN_ERR "DQS windows tuning: i=0x%x\n",i<<1);
#endif
#ifdef RTKEMMC_PHASE_TRACE
		trace_dqs_index = i;
#endif
		if(soc_device_match(rtk_soc_hank_a00)) {
			ret = rtkemmc_phase_tuning(emmc_port,MODE_HS400,0, TUNING_STAGE1);
			rtkemmc_dqs_delay_tap(emmc_port, (i<<1));
			if(ret == 0)
				ret = rtkemmc_phase_tuning(emmc_port,MODE_HS400,0, TUNING_STAGE_BOTH);
		}
		else{
			rtkemmc_dqs_delay_tap(emmc_port, (i<<1));
			ret = rtkemmc_phase_tuning(emmc_port, MODE_HS400, 0, TUNING_STAGE_BOTH);
		}
		if( ret == 0 && rw_test_tuning(emmc_port, dqs_tuning_blk_addr)==0) {
			j++;
			bitmap |= (1<<i);
		}
		else {
			j=0;
		}
	}
	if(max==0) {
		if((++retry_count)<=32) {
			if(retry_cmd_dly_tap>0x3e) retry_cmd_dly_tap=0;
			printk(KERN_ERR "DQS_RETRY: dqs tap bitmap= 0x%x, EMMC_WCMD_CTRL=0x%x, update EMMC_WCMD_CTRL value 0x%x, retry: %d\n",
					bitmap, readl(emmc_port->emmc_membase + EMMC_WCMD_CTRL), retry_cmd_dly_tap, retry_count);
			rtkemmc_dump_register(emmc_port);

			clk_disable_unprepare(emmc_port->clk_en_emmc);
			clk_disable_unprepare(emmc_port->clk_en_emmc_ip);
			sync(emmc_port);

			reset_control_assert(emmc_port->rstc_emmc);
			sync(emmc_port);

			reset_control_deassert(emmc_port->rstc_emmc);
			sync(emmc_port);

			clk_prepare_enable(emmc_port->clk_en_emmc);
			clk_prepare_enable(emmc_port->clk_en_emmc_ip);
			sync(emmc_port);

			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__); //wait for clear 0x255c bit 0
			rtkemmc_restore_register(emmc_port);

			cmd_delay_tap_setting(emmc_port, (0x80|retry_cmd_dly_tap));

                        sync(emmc_port);
                        mdelay(10);
			rtkemmc_restore_l4_register(emmc_port);

			retry_cmd_dly_tap += 2;

                        goto retry;
                }
                else {
			printk(KERN_ERR RED_BOLD"Cannot find a proper dqs window..., dqs tap bitmap= 0x%x\n"RESET, bitmap);
			asm volatile("wait_done_loop4:");
			asm volatile("b wait_done_loop4");
                }
	}
	else {
		retry_count = 0;
		rtkemmc_dqs_delay_tap(emmc_port, (readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1)-2-((max/2)*2)));

		suspend_dqs = readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1);
#ifdef RTKEMMC_PHASE_TRACE
		printk(KERN_ERR "trace_dqs_counter=%d\n", trace_dqs_counter);
#endif
		printk(KERN_ERR "max sample point=%d, bitmap=0x%x, DQS=0x%x EMMC_WCMD_CTRL=0x%x\n",
				max, bitmap, readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1), readl(emmc_port->emmc_membase + EMMC_WCMD_CTRL));
#ifdef RTKEMMC_PHASE_TRACE
		trace_dqs_index = 999;
#endif
		rtkemmc_phase_tuning(emmc_port,MODE_HS400,1, TUNING_STAGE_BOTH);
		emmc_port->time_setting = 0;
		printk(KERN_ERR "HS400: final phase=0x%x\n", readl(emmc_port->crt_membase + SYS_PLL_EMMC1));
	}

#ifdef SHA256
	if(compare3)
		dma_free_coherent(emmc_port->dev, 32, compare3 , compare3_phy_addr);
	if(compare4)
		dma_free_coherent(emmc_port->dev, 32, compare4 , compare4_phy_addr);
	compare3 = NULL;
	compare4 = NULL;
#endif
        sync(emmc_port);
        mdelay(10);
	g_bTuning = 0;

	buf = nvmem_cell_read(emmc_port->cell, &buf_size);

	printk(KERN_ERR "save eMMC hs400 parameter to emmc dqs_tuning_blk_addr 0x%lx\n", dqs_tuning_blk_addr+1024);
	hs400_data[0] = HS400_VERSION;
	hs400_data[1] = (readl(emmc_port->crt_membase + SYS_PLL_EMMC1) & 0x000000f8) >> 3;
	hs400_data[2] = (readl(emmc_port->crt_membase + SYS_PLL_EMMC1) & 0x00001f00) >> 8;
	hs400_data[3] = readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1);
	hs400_data[4] = readl(emmc_port->emmc_membase + EMMC_WCMD_CTRL);
	hs400_data[5] = hs400_data[0]+ hs400_data[1]*2 + hs400_data[2]*3 + hs400_data[3]*4 + hs400_data[4]*5;
	hs400_data[6] = buf[0];
	hs400_data[7] = buf[1];
	hs400_data[8] = buf[2];

	printk(KERN_ERR "hs400 parameter: hs400_verion[0]=0x%x, hs400_TX[1]=0x%x, hs400_RX[2]=0x%x,hs400_dqs[3]=0x%x,hs400_cmd_dly_tap[4]=0x%x\n",
			hs400_data[0], hs400_data[1],hs400_data[2],hs400_data[3],hs400_data[4]);
	printk(KERN_ERR "UUID: [0]=0x%x, [1]=0x%x, [2]=0x%x\n", hs400_data[6], hs400_data[7], hs400_data[8]);
	rtkemmc_send_cmd25(emmc_port, 512, dqs_tuning_blk_addr+1024, 1, hs400_data, 0);

	kfree(buf);

	if(emmc_port->retune==0)
		up_write(&emmc_port->cr_rw_sem);

}

static u32 rtkemmc_get_cmd_timeout(struct sd_cmd_pkt *cmd_info)
{
	struct rtkemmc_host *emmc_port = cmd_info->emmc_port;
	u16 block_count = cmd_info->block_count;
	u32 tmout = 0;

	MMCPRINTF("\n");

	if(cmd_info->cmd->data) {
		tmout = msecs_to_jiffies(2000);

		if(block_count>0x100)
			tmout = tmout + 1000 * msecs_to_jiffies(block_count);
    	}
	else
		tmout = msecs_to_jiffies(800);

#ifdef CONFIG_ANDROID
	tmout += msecs_to_jiffies(100);
#endif

	cmd_info->timeout = emmc_port->tmout = tmout;

	return 0;
}

static void rtkemmc_set_bus_width(struct rtkemmc_host *emmc_port, struct mmc_ios *ios)
{
	unsigned long flags;

	if (ios->bus_width == MMC_BUS_WIDTH_8){
		spin_lock_irqsave(&emmc_port->lock,flags);
		rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R) &
				EMMC_EXT_DAT_XFER_MASK) | EMMC_BUS_WIDTH_8,
				emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
		spin_unlock_irqrestore(&emmc_port->lock, flags);

		pr_info("%s: set bus width 8, EMMC_HOST_CTRL1_R=%08x\n",
			DRIVER_NAME, readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R));

        }
	else if (ios->bus_width == MMC_BUS_WIDTH_4){
		spin_lock_irqsave(&emmc_port->lock,flags);
		rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R) &
				(EMMC_EXT_DAT_XFER_MASK & EMMC_DAT_XFER_WIDTH_MASK))|EMMC_BUS_WIDTH_4,
				emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
		spin_unlock_irqrestore(&emmc_port->lock, flags);

		pr_info("%s: set bus width 4, EMMC_HOST_CTRL1_R=%08x\n",
			DRIVER_NAME, readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R));
	}
}

static void rtkemmc_set_ios(struct mmc_host *host, struct mmc_ios *ios)
{
	struct rtkemmc_host *emmc_port;
	u32 cur_timing = 0;

	emmc_port = mmc_priv(host);
	cur_timing = ios->timing;

	if (!g_bResuming) {
		switch(cur_timing)
		{
		case MMC_TIMING_MMC_HS400:
			rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R) &
				EMMC_HIGH_SPEED_MASK) | EMMC_HIGH_SPEED_EN,
				emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
			rtkemmc_writew((readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R) &
				EMMC_UHS_MODE_SEL_MASK)|MODE_HS400,
				emmc_port->emmc_membase + EMMC_HOST_CTRL2_R); //enable HS400
			if(soc_device_match(rtk_soc_hank))
				rtkemmc_set_freq(emmc_port,0xa6, 0x0);  //200MHZ
			else
				rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV1);
			break;
		case MMC_TIMING_MMC_HS200:
			rtkemmc_writew((readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R) &
				EMMC_UHS_MODE_SEL_MASK)|MODE_HS200,
				emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
				if(soc_device_match(rtk_soc_hank))
					rtkemmc_set_freq(emmc_port,0xa6, 0x0);  //200MHZ
				else
					rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV1);
			break;
		case MMC_TIMING_MMC_DDR52:
			rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R) &
				EMMC_HIGH_SPEED_MASK) | EMMC_HIGH_SPEED_EN,
				emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
			rtkemmc_writew((readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R) &
				EMMC_UHS_MODE_SEL_MASK) | MODE_DDR,
				emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
			if(soc_device_match(rtk_soc_hank))
				rtkemmc_set_freq(emmc_port,0x57, 0x1);  //50MB
			else
				rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV4);
			break;
		case MMC_TIMING_MMC_HS:
			rtkemmc_writew((readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R)&
				EMMC_UHS_MODE_SEL_MASK) | MODE_SDR,
				emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
			if(soc_device_match(rtk_soc_hank))
				rtkemmc_set_freq(emmc_port,0x57, 0x1);  //50Mhz
			else
				rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV4);
			/*enable L4 gated after SDR50 finished*/
			rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&0xfffffffe, emmc_port->emmc_membase+EMMC_OTHER1);
			break;
		case MMC_TIMING_LEGACY:
			if(soc_device_match(rtk_soc_hank))
				rtkemmc_set_freq(emmc_port,0x46, 0x80);  //80Mhz
			else
				rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV512);
			rtkemmc_writew((readw(emmc_port->emmc_membase + EMMC_HOST_CTRL2_R) &
				EMMC_UHS_MODE_SEL_MASK) | MODE_LEGACY,
				emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
			break;
		default:
			printk(KERN_INFO "%s: cur_timing = %u \n", __FILE__, cur_timing);
			break;
		}
	}
	rtkemmc_set_bus_width(emmc_port, ios);
	emmc_port->time_setting = 1;
}

static void rtkemmc_req_end_tasklet(unsigned long param)
{
	struct rtkemmc_host *emmc_port;
	struct mmc_request* mrq;
	unsigned long flags;
	MMCPRINTF("%s \n", __func__);

	emmc_port = (struct rtkemmc_host *)param;
	spin_lock_irqsave(&emmc_port->lock,flags);

	mrq = emmc_port->mrq;
	emmc_port->mrq = NULL;

	spin_unlock_irqrestore(&emmc_port->lock, flags);

	mmc_request_done(emmc_port->mmc, mrq);
}

static int rtkemmc_free_dma_buf(struct rtkemmc_host *emmc_port)
{
        if (emmc_port->desc_vaddr)
                dma_free_coherent(emmc_port->dev,
                                DESC_ALLOC_LENGTH,
                                emmc_port->desc_vaddr,
                                emmc_port->desc_paddr);

        if (emmc_port->dma_vaddr)
                dma_free_coherent(emmc_port->dev,
                                DMA_ALLOC_LENGTH,
                                emmc_port->dma_vaddr,
                                emmc_port->dma_paddr);

        return 0;
}

static int rtkemmc_allocate_dma_buf(struct rtkemmc_host *emmc_port)
{
        if (!emmc_port->desc_vaddr)
                emmc_port->desc_vaddr = dma_alloc_coherent(emmc_port->dev,
                                                        DESC_ALLOC_LENGTH,
                                                        &emmc_port->desc_paddr,
                                                        GFP_KERNEL);

        if (!emmc_port->dma_vaddr)
                emmc_port->dma_vaddr = dma_alloc_coherent(emmc_port->dev,
                                                        DMA_ALLOC_LENGTH,
                                                        &emmc_port->dma_paddr,
                                                        GFP_KERNEL);

        if(!emmc_port->desc_vaddr || !emmc_port->dma_vaddr)
        {
                pr_err("Allocate Realtek eMMC DMA failed !!!\n");
                return -ENOMEM;
        }

        return 0;
}

static int wait_done_timeout(struct rtkemmc_host *emmc_port, volatile u32 *addr, u32 mask, u32 value, const char *string)
{
	int n = 0;
	while(1)
	{
		if (((*addr) &mask) == value){
			break;
                }

		if((readw(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R) & 0x8000)!=0) {
			break;
		}

		if(n++ > 3000000) {
			pr_err("Timeout!!! cmd_opcode=%d, cmd_arg=0x%x, addr=0x%x, mask=0x%x, value=0x%x, emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R=0x%x \
				emmc_port->emmc_membase + EMMC_ERROR_INT_STA_R=0x%x, *addr=0x%x, pre_func=%s\n",
				emmc_port->cmd_opcode, readl(emmc_port->emmc_membase+EMMC_ARGUMENT_R), addr, mask, value,
				readw(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R), readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R),
				readl(addr), string);
#ifdef RTKEMMC_PHASE_TRACE
			if(g_bTuning==1) {
				printk(KERN_ERR "trace_cur_tuning_cmd = %d, trace_index=%d, trace_phase_bitmap=0x%x, trace_dqs_index=%d\n",
					trace_cur_tuning_cmd, trace_index, trace_phase_bitmap, trace_dqs_index);
				printk(KERN_ERR "last tuning err status=0x%x, last tuning normal status=0x%x, last tuning auto err status=0x%x\n",
					trace_err_status, trace_normal_status, trace_auto_err_status);
				printk(KERN_ERR "%s: trace_TX_window=0x%x, trace_RX_window=0x%x, trace_TX1_window=0x%x\n",
					__func__, trace_TX_window, trace_RX_window, trace_TX1_window);
			}
#endif
			printk(KERN_ERR "RESP01=0x%x, RESP23=0x%x, RESP45=0x%x, RESP67=0x%x\n",
				readl(emmc_port->emmc_membase + EMMC_RESP01_R),
				readl(emmc_port->emmc_membase + EMMC_RESP23_R),
				readl(emmc_port->emmc_membase + EMMC_RESP45_R),
				readl(emmc_port->emmc_membase + EMMC_RESP67_R));
			print_err_reg(emmc_port->cmd_opcode, emmc_port->normal_interrupt, emmc_port->error_interrupt);
                        print_ip_desc(emmc_port);
                        print_reg_info(emmc_port);
#ifdef RTKEMMC_PHASE_TRACE
			print_desc_content(emmc_port);
#endif
			asm volatile("wait_done_loop:");
			asm volatile("b wait_done_loop");
			return -1;
		}
		udelay(1);
		sync(emmc_port);
	}
	return 0;
}

void rtkemmc_set_pad_driving(struct rtkemmc_host *emmc_port, u32 clk_drv, u32 cmd_drv, u32 data_drv, u32 ds_drv)
{
	if(soc_device_match(rtk_soc_hank) || soc_device_match(rtk_soc_groot)) {
		u32 clk_drv_t;
		u32 cmd_drv_t;
		u32 data_drv_t;

		if((clk_drv+1)>7) clk_drv_t= 7;
		else clk_drv_t = clk_drv+1;

		if((cmd_drv+1)>7) cmd_drv_t=7;
		else cmd_drv_t = cmd_drv+1;

		if((data_drv+1)>7) data_drv_t=7;
		else data_drv_t = data_drv+1;

		rtkemmc_writel((readl(emmc_port->mux_mis_membase + EMMC_ISO_PFUNC1)&0xff03f03f)|(clk_drv_t<<6)|(clk_drv<<9)|(cmd_drv_t<<18)|(cmd_drv<<21),
				emmc_port->mux_mis_membase + EMMC_ISO_PFUNC1);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase + EMMC_ISO_PFUNC2)&0xff03f03f)|(data_drv_t<<6)|(data_drv<<9)|(data_drv_t<<18)|(data_drv<<21),
				emmc_port->mux_mis_membase + EMMC_ISO_PFUNC2);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase + EMMC_ISO_PFUNC3)&0xff03f03f)|(data_drv_t<<6)|(data_drv<<9)|(data_drv_t<<18)|(data_drv<<21),
				emmc_port->mux_mis_membase + EMMC_ISO_PFUNC3);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase + EMMC_ISO_PFUNC4)&0xff03f03f)|(data_drv_t<<6)|(data_drv<<9)|(data_drv_t<<18)|(data_drv<<21),
				emmc_port->mux_mis_membase + EMMC_ISO_PFUNC4);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase + EMMC_ISO_PFUNC5)&0xff03f03f)|(data_drv_t<<6)|(data_drv<<9)|(data_drv_t<<18)|(data_drv<<21),
				emmc_port->mux_mis_membase + EMMC_ISO_PFUNC5);

	}
	else {
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC4)&0xfff81fff)|(clk_drv<<13)|(clk_drv<<16),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC4);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC5)&0xfffff03f)|(cmd_drv<<6)|(cmd_drv<<9),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC5);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC6)&0xfff03fff)|(data_drv<<14)|(data_drv<<17),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC6);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC7)&0xfe07f03f)|(data_drv<<6)|(data_drv<<9)|(data_drv<<19)|(data_drv<<22),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC7);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC8)&0xfff81fc0)|(data_drv<<0)|(data_drv<<3)|(data_drv<<13)|(data_drv<<16),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC8);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC9)&0xfe07f03f)|(data_drv<<6)|(data_drv<<9)|(clk_drv<<19)|(clk_drv<<22),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC9);
		rtkemmc_writel((readl(emmc_port->mux_mis_membase+ EMMC_STARK_ISO_PFUNC10)&0xffffffc0)|(clk_drv<<0)|(clk_drv<<3),
			emmc_port->mux_mis_membase + EMMC_STARK_ISO_PFUNC10);
	}
	isb();
	sync(emmc_port);
}

void phase(struct rtkemmc_host *emmc_port, u32 VP0, u32 VP1)
{
	if((VP0!=0xff) || (VP1!=0xff))
	{
		if(soc_device_match(rtk_soc_hank)) {
			rtkemmc_writel((readl(emmc_port->emmc_membase + EMMC_CKGEN_CTL)|0x70000),
				emmc_port->emmc_membase + EMMC_CKGEN_CTL);     //change clock to 4MHz
			sync(emmc_port);
			rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xfffffffd),
				emmc_port->crt_membase + SYS_PLL_EMMC1);
			if(VP0!=0xff)
				rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xffffff07)|(VP0<<3),
					emmc_port->crt_membase + SYS_PLL_EMMC1);

			if(VP1!=0xff)
				rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xffffe0ff)|(VP1<<8),
					emmc_port->crt_membase + SYS_PLL_EMMC1);
			rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)|0x2),
				emmc_port->crt_membase + SYS_PLL_EMMC1);
			sync(emmc_port);
			udelay(200);

			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__); //wait for clear 0x255c bit 0
			rtkemmc_writel((readl(emmc_port->emmc_membase + EMMC_CKGEN_CTL)&0xfff8ffff),
				emmc_port->emmc_membase + EMMC_CKGEN_CTL);  //change clock to PLL
			sync(emmc_port);
		}
		else {
			u32 t1=10; //us, after phrt0 = 0
			u32 t2=3; //us, after phse setup

			rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xfffffffd),
				emmc_port->crt_membase + SYS_PLL_EMMC1);
			udelay(t1);

			rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)|(1<<10),
				emmc_port->emmc_membase+EMMC_OTHER1);
			if(VP0!=0xff)
				rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xffffff07)|(VP0<<3),
					emmc_port->crt_membase + SYS_PLL_EMMC1);

			if(VP1!=0xff)
				rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xffffe0ff)|(VP1<<8),
					emmc_port->crt_membase + SYS_PLL_EMMC1);
			udelay(t2);

			rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&(~(1<<10)),
				emmc_port->emmc_membase+EMMC_OTHER1);
			rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)|0x2),
				emmc_port->crt_membase + SYS_PLL_EMMC1);

			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__);
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), 0x3, 0x3, __func__);
		}
		rtkemmc_writeb(0x6, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
		wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x6<<24), 0x0, __func__);
	}
}

static void pll_setup(struct rtkemmc_host *emmc_port, u32 freq)
{
	u32 sscpll_icp = 1;
	unsigned int tmp_val=0;

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xfffffffd), emmc_port->crt_membase + SYS_PLL_EMMC1);
        udelay(10);      //

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC4) & 0x06);
	rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC4);

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC3) & 0xffff)|(freq<<16);
        rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC3);
	udelay(3);

	if(soc_device_match(rtk_soc_stark)) {
		if(freq<98)
			sscpll_icp = 0;

		rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC2)&0xfffffc1f)|(sscpll_icp<<5),
			emmc_port->crt_membase + SYS_PLL_EMMC2); //f=0, rs=5
	}

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC4) | 0x01);
	rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC4);

	udelay(60);

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)|0x2), emmc_port->crt_membase + SYS_PLL_EMMC1);
        udelay(10);

	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__);
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), 0x3, 0x3, __func__);
	rtkemmc_writeb(0x6, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x6<<24), 0x0, __func__);
}

static void rtkemmc_stark_set_freq(struct rtkemmc_host *emmc_port, u32 freq, u32 div_ip)
{
	pll_setup(emmc_port, freq);

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xfffffffd), emmc_port->crt_membase + SYS_PLL_EMMC1);     //reset pll
	udelay(6);

	switch(div_ip) {
	case EMMC_CLK_DIV1:
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) &
			(~(EMMC_CRC_CLK_DIV_EN|EMMC_CLK_INV_DIV_SEL)),
			emmc_port->emmc_membase+EMMC_CKGEN_CTL); //[9:8]='b00
		break;
	case EMMC_CLK_DIV4:
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) &
			(~(EMMC_CRC_CLK_DIV_EN|EMMC_CLK_INV_DIV_SEL)) |
			EMMC_CRC_CLK_DIV_EN|EMMC_CLK_INV_DIV_SEL,
			emmc_port->emmc_membase+EMMC_CKGEN_CTL); //[9:8]='b11
		break;
	case EMMC_CLK_DIV512:
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) &
			(~(EMMC_CRC_CLK_DIV_EN|EMMC_CLK_INV_DIV_SEL)) |
			EMMC_CRC_CLK_DIV_EN,
			emmc_port->emmc_membase+EMMC_CKGEN_CTL); //[9:8]='b01
		break;
	default:
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) &
			(~(EMMC_CRC_CLK_DIV_EN|EMMC_CLK_INV_DIV_SEL)),
			emmc_port->emmc_membase+EMMC_CKGEN_CTL); //[9:8]='b00
		break;
	}

	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) & ~(1<<20),
		emmc_port->emmc_membase+EMMC_CKGEN_CTL);

	udelay(6);

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)|0x2), emmc_port->crt_membase + SYS_PLL_EMMC1);    //release reset pll
	udelay(6);

	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__);
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), 0x3, 0x3, __func__);
	rtkemmc_writeb(0x6, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x6<<24), 0x0, __func__);

	printk(KERN_INFO "%s: div_ip=0x%08x, PLL_EMMC1=%08x, PLL_EMMC2=%08x, PLL_EMMC3=%08x, PLL_EMMC4=%08x\n",
		DRIVER_NAME,
		readl(emmc_port->emmc_membase + EMMC_CKGEN_CTL),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC1),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC2),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC3),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC4));
}

static void rtkemmc_set_freq(struct rtkemmc_host *emmc_port, u32 freq, u32 div_ip)
{
	u32 tmp_val=0;
	unsigned long flags;

	spin_lock_irqsave(&emmc_port->lock,flags);

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC4) & 0x06);
	rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC4);
	isb();
	sync(emmc_port);

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC3) & 0xffff)|(freq<<16);
	rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC3);
	isb();
	sync(emmc_port);

	tmp_val = (readl(emmc_port->crt_membase + SYS_PLL_EMMC4) | 0x01);
	rtkemmc_writel(tmp_val, emmc_port->crt_membase + SYS_PLL_EMMC4);
	isb();
	sync(emmc_port);

	udelay(100);    //wait clock stable after crt reset
	rtkemmc_dump_register(emmc_port);

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)&0xfffffffd), emmc_port->crt_membase + SYS_PLL_EMMC1);     //reset pll
	sync(emmc_port);

	clk_disable_unprepare(emmc_port->clk_en_emmc);
	clk_disable_unprepare(emmc_port->clk_en_emmc_ip);
	sync(emmc_port);

	reset_control_assert(emmc_port->rstc_emmc);
	sync(emmc_port);

	udelay(3);

	reset_control_deassert(emmc_port->rstc_emmc);
	sync(emmc_port);

	clk_prepare_enable(emmc_port->clk_en_emmc);
	clk_prepare_enable(emmc_port->clk_en_emmc_ip);
	sync(emmc_port);

	udelay(3);

	rtkemmc_restore_register(emmc_port);

	if(div_ip!=0) {
		rtkemmc_writel(((readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL) &
			EMMC_CRC_CLK_DIV_MASK) | div_ip) | EMMC_CRC_CLK_DIV_EN,
			emmc_port->emmc_membase+EMMC_CKGEN_CTL); //set the enable bit
	}

	rtkemmc_writel((readl(emmc_port->crt_membase + SYS_PLL_EMMC1)|0x2), emmc_port->crt_membase + SYS_PLL_EMMC1);    //release reset pll
	sync(emmc_port);

	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x55c), 0x1, 0x1, __func__); //wait for clear 0x255c bit
	udelay(100);

	rtkemmc_restore_l4_register(emmc_port);

	printk(KERN_ERR "%s: emmc_port->emmc_membase+EMMC_CKGEN_CTL = 0x%x\n",__func__, readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL));
	spin_unlock_irqrestore(&emmc_port->lock, flags);

	printk(KERN_INFO "%s: div_ip=0x%08x, PLL_EMMC1=%08x, PLL_EMMC2=%08x, PLL_EMMC3=%08x, PLL_EMMC4=%08x\n",
		DRIVER_NAME,
		readl(emmc_port->emmc_membase + EMMC_CKGEN_CTL),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC1),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC2),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC3),
		readl(emmc_port->crt_membase + SYS_PLL_EMMC4));
}

static void rtkemmc_set_pin_mux(struct rtkemmc_host *emmc_port)
{
	MMCPRINTF("rtkemmc_set_pin_mux \n");
	
	rtkemmc_writel((readl(emmc_port->mux_mis_membase)&0xff000000)|0x00aaaaaa, emmc_port->mux_mis_membase); //pad mux
	sync(emmc_port);
}

#if 0
static int rtk_gic_peek_irq(struct rtkemmc_host *emmc_port, u32 irq_num, u32 offset)
{
	u32 hw_irq = irq_num + 32;	//0-31 is occuppied by system
	u32 mask = 1 << (hw_irq % 32);

	return !!(readl_relaxed(emmc_port->gicd_membase + offset + (hw_irq / 32) * 4) & mask);
}
#endif

static void rtkemmc_timeout_timer(struct timer_list *t)
{
	struct rtkemmc_host *emmc_port = from_timer(emmc_port, t, timer);
	unsigned long flags;

	pr_err("cpuid=%d, opcode=%d, rtkemmc_timeout_timer fired ...\n",
			raw_smp_processor_id(), emmc_port->cmd_opcode);
#if 0
	printk(KERN_ERR "emmc irq %d status: IsPender=%d, IsActiver=%d, IsEnabler=%d\n",
		emmc_port->irq_num+32,
		rtk_gic_peek_irq(emmc_port, emmc_port->irq_num, 0x0200),
		rtk_gic_peek_irq(emmc_port, emmc_port->irq_num, 0x0300),
		rtk_gic_peek_irq(emmc_port, emmc_port->irq_num, 0x0100));
#endif
#ifdef RTKEMMC_PHASE_TRACE
	if(g_bTuning==1) {
		pr_err("trace_cur_tuning_cmd = %d, trace_index=%d, trace_phase_bitmap=0x%x, trace_dqs_index=%d\n",
			trace_cur_tuning_cmd, trace_index, trace_phase_bitmap, trace_dqs_index);
		pr_err("last tuning err status=0x%x, last tuning normal status=0x%x, last tuning auto err status=0x%x\n",
			trace_err_status, trace_normal_status, trace_auto_err_status);
		pr_err("%s: trace_TX_window=0x%x, trace_RX_window=0x%x, trace_TX1_window=0x%x\n",
			__func__, trace_TX_window, trace_RX_window, trace_TX1_window);
	}
#endif

	MMCPRINTF("%s - int_wait=%08x\n", __func__, emmc_port->int_waiting);

	spin_lock_irqsave(&emmc_port->lock,flags);
	if(emmc_port->int_waiting)
	{
		pr_err("%s: before clear signal interrupt\n", __func__);
		pr_err("%s: 0x98012038 NORMAL INTERRUPT SIGNAL EN= 0x%x\n", __func__, readw(emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R));
		pr_err("%s: 0x9801203a ERROR INTERRUPT SIGNAL EN = 0x%x\n", __func__, readw(emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R));

		rtkemmc_hold_int_dec();
		rtkemmc_get_int_sta(&emmc_port->normal_interrupt, &emmc_port->error_interrupt, &emmc_port->auto_error_interrupt);
		sync(emmc_port);
		pr_err("%s: normal_interrupt =%08x, error_interrupt=0x%08x, auto_error_interrupt=0x%08x\n",
				__func__, emmc_port->normal_interrupt, emmc_port->error_interrupt, emmc_port->auto_error_interrupt);

		print_ip_desc(emmc_port);
		print_reg_info(emmc_port);
	}
	else {
		WARN_ON(1);
	}

	if(emmc_port->int_waiting)
		complete(emmc_port->int_waiting);

	spin_unlock_irqrestore(&emmc_port->lock, flags);
}

static void rtkemmc_interrupt_err_query(u16 intmask, int *cmd_error, int *data_error)
{
	if (intmask & (EMMC_CMD_IDX_ERR | EMMC_CMD_END_BIT_ERR | EMMC_CMD_CRC_ERR))
                *cmd_error = -EILSEQ;
        else if (intmask & EMMC_CMD_TOUT_ERR)
                *cmd_error = -ETIMEDOUT;
        else
                *cmd_error = 0;

        if (intmask & (EMMC_DATA_END_BIT_ERR | EMMC_DATA_CRC_ERR))
                *data_error = -EILSEQ;
        else if (intmask & EMMC_DATA_TOUT_ERR)
                *data_error = -ETIMEDOUT;
        else if (intmask & EMMC_ADMA_ERR)
                *data_error = -EIO;
        else
                *data_error = 0;
}

static irqreturn_t rtkemmc_irq(int irq, void *dev)
{
	struct rtkemmc_host *emmc_port = dev;
	struct cqhci_host *cq_host = NULL;
	int cmd_error=0, data_error=0;

	if(emmc_port->cmdq==1)
		cq_host = emmc_port->mmc->cqe_private;


	rtkemmc_get_int_sta(&emmc_port->normal_interrupt, &emmc_port->error_interrupt, &emmc_port->auto_error_interrupt);
	sync(emmc_port);

	if(emmc_port->cmdq==1) {
		if(emmc_port->mmc->cqe_on==false && cq_host->activated==false)
			rtkemmc_hold_int_dec();
	} else {
		rtkemmc_hold_int_dec();
	}

	if(emmc_port->cmdq==1 && emmc_port->mmc->cqe_on==true && cq_host->activated==true) {	//if we run the cmdq mode currently
		if(emmc_port->normal_interrupt&0x8000) {
                                pr_err("%s: cmdq error case: cpuid=%d, normal_interrupt =%08x, error_interrupt=0x%08x, EMMC_CQIS=0x%x, EMMC_CQTCN=0x%x\n",
                                        __func__, raw_smp_processor_id(), emmc_port->normal_interrupt, emmc_port->error_interrupt,
					readl(emmc_port->cq_host->mmio+CQHCI_IS), readl(emmc_port->cq_host->mmio+CQHCI_TCN));
				rtkemmc_interrupt_err_query(emmc_port->error_interrupt, &cmd_error, &data_error);

                                rtkemmc_cqhci_dumpregs(emmc_port->cq_host);
				print_ip_desc(emmc_port);
				print_reg_info(emmc_port);
                }
		cqhci_irq(emmc_port->mmc, (u32)(emmc_port->normal_interrupt), cmd_error, data_error);
		rtkemmc_clr_int_sta();	//clear 0x2030 status

		return IRQ_HANDLED;
	}

#ifdef RTKEMMC_DEBUG
	printk(KERN_ERR "%s_legacy: cpuid=%d, cmd_idx=%d, normal_interrupt =%08x, error_interrupt=0x%08x\n",
			__func__, raw_smp_processor_id(), emmc_port->cmd_opcode, emmc_port->normal_interrupt, emmc_port->error_interrupt);
#endif
	if(emmc_port->int_waiting) {
		trace_mmc_rtkemmc_legacy_irq_complete(emmc_port->normal_interrupt, emmc_port->error_interrupt);
		del_timer(&emmc_port->timer);
		if (g_bResuming) //avoid to emit complete in rtkemmc_resume
			return IRQ_HANDLED;
		complete(emmc_port->int_waiting);
	}

	sync(emmc_port);

	return IRQ_HANDLED;
}

static void rtkemmc_init(struct rtkemmc_host *emmc_port)
{
	struct mmc_host *host=emmc_port->mmc;
	unsigned long flags;

	MMCPRINTF("%s : \n", __func__);

#ifdef PHASE_INHERITED
        if (emmc_port->tx_phase == 0xff && emmc_port->rx_phase==0xff){
                HS200_TX = emmc_port->tx_phase = (readl(emmc_port->crt_membase + SYS_PLL_EMMC1) & 0x000000f8) >> 3;
                HS200_RX = emmc_port->rx_phase = (readl(emmc_port->crt_membase + SYS_PLL_EMMC1) & 0x00001f00) >> 8;
        }

#endif
	if(emmc_port->tx_user_defined) {        //if we set user defined tx and rx value, then we won't use the bootcode reference value, always the first priority
                HS200_TX = emmc_port->tx_phase = emmc_port->tx_reference_phase;
        }
        if(emmc_port->rx_user_defined) {
                HS200_RX = emmc_port->rx_phase = emmc_port->rx_reference_phase;
        }

#ifdef DQS_INHERITED
	if (emmc_port->dqs == 0xff)
		emmc_port->dqs = readl(emmc_port->emmc_membase + EMMC_DQS_CTRL1);
#endif
	rtkemmc_writel(3, emmc_port->crt_membase + SYS_PLL_EMMC1);      //980001f0

	rtkemmc_reset_fifo(emmc_port);

	isb();
	sync(emmc_port);

	rtkemmc_writeb(0x07, emmc_port->emmc_membase + EMMC_SW_RST_R);      //9801202f, Software Reset Register
	isb();
	sync(emmc_port);
	
	rtkemmc_writeb(0x0e, emmc_port->emmc_membase + EMMC_TOUT_CTRL_R);      //9801202e, Timeout Control register
        isb();
        sync(emmc_port);

	rtkemmc_writew(0x200, emmc_port->emmc_membase + EMMC_BLOCKSIZE_R);      //98012004, block size = 512Byte
        isb();
        sync(emmc_port);

	rtkemmc_writew(0x1008 ,emmc_port->emmc_membase + EMMC_HOST_CTRL2_R);
	isb();
	sync(emmc_port);

	rtkemmc_writew(0xfeff, emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_EN_R);	//98012034, enable all Normal Interrupt Status register
	isb();
	sync(emmc_port);

	rtkemmc_writew(EMMC_ALL_ERR_STAT_EN, emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_EN_R);	//98012036, enable all error Interrupt Status register
	isb();
	sync(emmc_port);

	rtkemmc_writew(0xfeff,emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R);	//98012038, enable all Normal SIGNAL Interrupt  register
	isb();
	sync(emmc_port);

	rtkemmc_writew(EMMC_ALL_ERR_SIGNAL_EN,emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R);	//9801203a, enable all error SIGNAL Interrupt register
	isb();
	sync(emmc_port);

	rtkemmc_writeb(0x0d, emmc_port->emmc_membase + EMMC_CTRL_R);      //9801202f, choose is card emmc bit
	isb();
	sync(emmc_port);

	if(soc_device_match(rtk_soc_hank_a00)) {	//in hank A00, cannot enable L4 gated in low speed
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)|0x1, emmc_port->emmc_membase+EMMC_OTHER1);        //disable L4 gated
	}
	else {
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)&0xfffffffa, emmc_port->emmc_membase+EMMC_OTHER1);        //enable L4 gated
	}

	isb();
	sync(emmc_port);

	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_DUMMY_SYS)|(EMMC_CLK_O_ICG_EN|EMMC_CARD_STOP_ENABLE), emmc_port->emmc_membase+EMMC_DUMMY_SYS);	//enable eMMC command clock
	isb();
	sync(emmc_port);

	rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R)&0xe7)|(EMMC_ADMA2_32<<EMMC_DMA_SEL),
                        emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);   //ADMA2 32 bit select
	isb();
        sync(emmc_port);

	rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_MSHC_CTRL_R) & (~EMMC_CMD_CONFLICT_CHECK)), emmc_port->emmc_membase + EMMC_MSHC_CTRL_R);	//disable emmc cmd conflict checkout
	if(soc_device_match(rtk_soc_stark) || soc_device_match(rtk_soc_groot)) {
		rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_OTHER1)|EMMC_STARK_CARD_STOP_ENABLE,
			emmc_port->emmc_membase+EMMC_OTHER1);
	}

	isb();
	sync(emmc_port);

	rtkemmc_writew(readw(emmc_port->emmc_membase + EMMC_CLK_CTRL_R)|0x1, emmc_port->emmc_membase + EMMC_CLK_CTRL_R);   //enable internal clock
	isb();
	sync(emmc_port);

	rtkemmc_writel(0x1, emmc_port->m2tmx_membase+EMMC_NAND_DMA_SEL);	// #sram_ctrl, 0 for nf, 1 for emmc
        isb();
        sync(emmc_port);

	rtkemmc_writel(readl(emmc_port->emmc_membase+EMMC_AHB)|0x4,emmc_port->emmc_membase+EMMC_AHB);
	spin_lock_irqsave(&emmc_port->lock,flags);	
	//initially set bus width 1
	rtkemmc_writeb((readb(emmc_port->emmc_membase + EMMC_HOST_CTRL1_R)&0xdd)|EMMC_BUS_WIDTH_1,emmc_port->emmc_membase + EMMC_HOST_CTRL1_R);
	sync(emmc_port);
	spin_unlock_irqrestore(&emmc_port->lock, flags);

	rtkemmc_set_pad_driving(emmc_port, 0x0, 0x0, 0x0, 0x0);

	
	//initially, we do not use pass tuning result from bootcode, so we need to reset the tx ,rx phase to 0 first, 
	//and then framework call rtkemmc_tuning will set the rx tx register
	//Now, we use the mechanism and hold this part for fear that user will use kernel tuning one day in the future
	//if we use tuning inherit, the phase will be set to 0 and restore the tx rx from bootcode
	phase(emmc_port, 0, 0); //VP0, VP1 phase

	rtkemmc_reset_fifo(emmc_port);

	if(soc_device_match(rtk_soc_hank))
		rtkemmc_set_freq(emmc_port,0x46, 0x80);  //80Mhz
	else
		rtkemmc_stark_set_freq(emmc_port,0xa6, EMMC_CLK_DIV512);  //80Mhz
	sync(emmc_port);
	host->ops = &rtkemmc_ops;
}

static int rtkemmc_set_rspparam(struct rtkemmc_host *emmc_port, struct sd_cmd_pkt *cmd_info)
{
	switch(cmd_info->cmd->opcode)
	{
	case MMC_GO_IDLE_STATE:
		cmd_info->cmd_para = (EMMC_NO_RESP);
		cmd_info->rsp_len = 6;
		cmd_info->cmd->arg = 0x00000000;
		break;
	case MMC_SEND_OP_COND:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48);
		cmd_info->cmd->arg = MMC_SECTOR_ADDR|MMC_VDD_165_195;
		cmd_info->rsp_len = 6;
		break;
	case MMC_ALL_SEND_CID:
		cmd_info->cmd_para = (EMMC_RESP_LEN_136|EMMC_CMD_CHK_RESP_CRC);
		cmd_info->rsp_len = 17;
		cmd_info->cmd->arg = 0x00000000;
		break;
	case MMC_SET_RELATIVE_ADDR:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->cmd->arg = (1<<RCA_SHIFTER);
		cmd_info->rsp_len = 6;
		break;
	case MMC_SEND_CSD:
	case MMC_SEND_CID:
		cmd_info->cmd_para = (EMMC_RESP_LEN_136|EMMC_CMD_CHK_RESP_CRC);
		cmd_info->cmd->arg = (1<<RCA_SHIFTER);
		cmd_info->rsp_len = 17;
		break;
	case MMC_SEND_EXT_CSD:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->cmd->arg = 0;
		cmd_info->rsp_len = 6;
		break;
	case MMC_SLEEP_AWAKE:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48B|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->rsp_len = 6;
		printk(KERN_INFO "%s : cmd5 arg=0x%08x\n",__func__,cmd_info->cmd->arg);
		break;
	case MMC_SELECT_CARD:
		printk(KERN_INFO "%s : cmd7 arg : 0x%08x\n",__func__,cmd_info->cmd->arg);
		if (cmd_info->cmd->flags == (MMC_RSP_NONE | MMC_CMD_AC)) {
			printk(KERN_INFO "%s : cmd7 with rsp none\n",__func__);
			cmd_info->cmd_para = (EMMC_NO_RESP);
		}
		else {
			printk(KERN_INFO "%s : cmd7 with rsp\n",__func__);
			cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		}
		cmd_info->rsp_len = 6;
		break;
	case MMC_SWITCH:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48B|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->rsp_len = 6;
		break;
	case MMC_SEND_STATUS:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->cmd->arg = (1<<RCA_SHIFTER);
		cmd_info->rsp_len = 6;
		break;
	case MMC_STOP_TRANSMISSION:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|(EMMC_ABORT_CMD<<6));
		cmd_info->rsp_len = 6;
		break;
	case MMC_SEND_TUNING_BLOCK_HS200:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->cmd->arg = 0;
		cmd_info->rsp_len = 6;
	case MMC_READ_MULTIPLE_BLOCK:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->rsp_len = 6;
		break;
	case MMC_SET_BLOCK_COUNT:
	case MMC_SET_BLOCKLEN:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->rsp_len = 6;
		break;
	case MMC_WRITE_MULTIPLE_BLOCK:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->rsp_len = 6;
		break;
	case MMC_READ_SINGLE_BLOCK:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->rsp_len = 6;
		break;
	case MMC_WRITE_BLOCK:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->rsp_len = 6;
		break;
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48B|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->rsp_len = 6;
		break;
	case MMC_SEND_WRITE_PROT:
	case MMC_SEND_WRITE_PROT_TYPE:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
		cmd_info->rsp_len = 6;
		break;
	case MMC_ERASE_GROUP_START:
	case MMC_ERASE_GROUP_END:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
                cmd_info->rsp_len = 6;
		break;
	case MMC_ERASE:
		cmd_info->cmd_para = (EMMC_RESP_LEN_48B|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
		cmd_info->rsp_len = 6;
		break;
	case MMC_GEN_CMD:
		if(cmd_info->cmd->arg & 0x1) {   //read single block
			cmd_info->cmd->arg = 0x1;
			cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
			cmd_info->rsp_len = 6;
			break;
		}
		else {      //write single block
			cmd_info->cmd->arg = 0x0;
			cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
			cmd_info->rsp_len = 6;
			break;
		}
	case MMC_MANUFACTURER_CMD62:
		if(emmc_port->mmc->card->cid.manfid==0x45) {
			cmd_info->cmd_para = (EMMC_RESP_LEN_48B|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE);
			cmd_info->rsp_len = 6;
		}
		else {
			printk("command %d for this manufacturer is undefined in Realtek eMMC driver!!!\n", cmd_info->cmd->opcode);
			cmd_info->cmd_para = 0;
			cmd_info->rsp_len = 6;
		}
		break;
	case MMC_MANUFACTURER_CMD63:
		if(emmc_port->mmc->card->cid.manfid==0x45) {
			cmd_info->cmd_para = (EMMC_RESP_LEN_48|EMMC_CMD_CHK_RESP_CRC|EMMC_CMD_IDX_CHK_ENABLE|EMMC_DATA);
			cmd_info->rsp_len = 6;
		}
		else {
			printk("command %d for this manufacturer is undefined in Realtek eMMC driver!!!\n", cmd_info->cmd->opcode);
			cmd_info->cmd_para = 0;
			cmd_info->rsp_len = 6;
		}
		break;
	default:
		printk("Realtek Unrecognized eMMC command, cmd_idx=%d !!!\n", cmd_info->cmd->opcode);
		cmd_info->cmd_para = 0;
		cmd_info->rsp_len = 6;
		break;
	}

	MMCPRINTF(KERN_INFO "%s : cmd=0x%02x rsp_len=0x%02x arg=0x%08x para=0x%08x\n","rtkemmc", cmd_info->cmd->opcode, cmd_info->rsp_len,cmd_info->cmd->arg,cmd_info->cmd_para);
	return 0;
}

static int rtkemmc_stop_transmission(struct mmc_card *card,int bIgnore)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	int err = 0;
	int bMalloc=0;
	struct mmc_host * mmc = card->host;

	MMCPRINTF("%s : \n", __func__);

	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));
	if (card == NULL) {
		bMalloc=1;
		card = (struct mmc_card*)kmalloc(sizeof(struct mmc_card),GFP_KERNEL);
		card->host = mmc;
	}
	set_cmd_info(card,&cmd,&cmd_info, MMC_STOP_TRANSMISSION, 0x00, 6);
	err = SD_SendCMDGetRSP(&cmd_info,bIgnore);
	if (bMalloc) {
		kfree(card);
		card = NULL;
	}

	if(err)
		mmcmsg3(KERN_WARNING "%s: MMC_STOP_TRANSMISSION fail\n",DRIVER_NAME);

	return err;
}

static int rtkemmc_wait_status(struct mmc_card *card,u8 state,u8 divider,int bIgnore)
{
	struct mmc_command cmd;
	struct sd_cmd_pkt cmd_info;
	unsigned long timeend;
	int err, bMalloc=0;
	struct mmc_host * mmc = card->host;

	MMCPRINTF("\n");
	timeend = jiffies + msecs_to_jiffies(100);    /* wait 100ms */

	if (card == NULL) {
		bMalloc=1;
		card = (struct mmc_card*)kmalloc(sizeof(struct mmc_card),GFP_KERNEL);
		card->host = mmc;
	}

	do {
		memset(&cmd, 0, sizeof(struct mmc_command));
		memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

		set_cmd_info(card,&cmd,&cmd_info, MMC_SEND_STATUS, (card->rca)<<RCA_SHIFTER, 6);
		err = SD_SendCMDGetRSP(&cmd_info,bIgnore);

		if(err) {
			if (!bIgnore)
				printk(KERN_INFO "wait %s fail\n",state_tlb[state]);
			break;
		}
		else {
			u8 cur_state = R1_CURRENT_STATE(cmd.resp[0]);
			if (!bIgnore)
				MMCPRINTF(KERN_WARNING "resp[0]=0x%08x,cur_state=%s\n",cmd.resp[0],state_tlb[cur_state]);
			err = -9999;
			if(cur_state == state) {
				if(cmd.resp[0] & R1_READY_FOR_DATA) {
					err = 0;
					break;
				}
			}
		}
	}while(time_before(jiffies, timeend));

	if (bMalloc) {
		kfree(card);
		card = NULL;
	}
	return err;
}

static int rtkemmc_wait_opt_end(struct rtkemmc_host *emmc_port,u32 cmd_idx,u8 ignore_log)
{
	volatile int err = RTK_SUCC;
	volatile unsigned long timeend=0;
	unsigned long flags;
	u8 XFER_flag=0;
	u16 reg_blksize = 0;
	u16 reg_blkcount = 0;
	u16 reg_cmdidx = 0;
	u32 reg_argu = 0;

	unsigned long msec = TIMEOUT_MS;
	
	switch(CMD_IDX_MASK(cmd_idx)) {
		case MMC_READ_SINGLE_BLOCK:
		case MMC_READ_MULTIPLE_BLOCK:
		case MMC_WRITE_BLOCK:
		case MMC_WRITE_MULTIPLE_BLOCK:
		case MMC_SEND_EXT_CSD:
		case MMC_GEN_CMD:
		case MMC_SLEEP_AWAKE:
		case MMC_SWITCH:
		case MMC_SET_WRITE_PROT:
		case MMC_CLR_WRITE_PROT:
		case MMC_SEND_WRITE_PROT:
		case MMC_SEND_WRITE_PROT_TYPE:
		case MMC_ERASE:
		case MMC_SEND_TUNING_BLOCK_HS200:
			XFER_flag=1;
			break;
		case MMC_MANUFACTURER_CMD62:
		case MMC_MANUFACTURER_CMD63:
			if(emmc_port->mmc->card->cid.manfid==0x45)
				XFER_flag=1;
			break;
	}
	emmc_port->int_waiting = &rtk_emmc_wait;
	/* timeout timer fire */
	timeend = jiffies + msecs_to_jiffies(msec + emmc_port->tmout);
	mod_timer(&emmc_port->timer, timeend);

	if (emmc_port->int_waiting) {
		rtkemmc_hold_int_dec();
		rtkemmc_clr_int_sta();
		emmc_port->int_waiting = &rtk_emmc_wait;

		if (XFER_flag==1)
			rtkemmc_en_xfer_int();	//command with data, r1b case
		else
			rtkemmc_en_cd_int(); //command case

		if (CMD_IDX_MASK(cmd_idx)==MMC_WRITE_MULTIPLE_BLOCK || CMD_IDX_MASK(cmd_idx)==MMC_READ_MULTIPLE_BLOCK ){
			if (emmc_port->rpmb_cmd) {
				rtkemmc_writew(readw(emmc_port->emmc_membase + EMMC_XFER_MODE_R) & ~(1<<EMMC_AUTO_CMD_ENABLE),
					emmc_port->emmc_membase + EMMC_XFER_MODE_R);
				emmc_port->rpmb_cmd = 0;
			}//CMD25/18 following CMD13, never set CMD_SEND_AUTO_STOP
                }

		if(CMD_IDX_MASK(cmd_idx)==MMC_SEND_TUNING_BLOCK_HS200) {
			rtkemmc_writew(0x80, emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
		}
		else if(CMD_IDX_MASK(cmd_idx)==MMC_SEND_WRITE_PROT) {
			rtkemmc_writew(0x4, emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
		}
		else if(CMD_IDX_MASK(cmd_idx)==MMC_SEND_WRITE_PROT_TYPE) {
			rtkemmc_writew(0x8, emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
		}
		else {
			rtkemmc_writew(0x200, emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
		}
		//cmd fire
		spin_lock_irqsave(&emmc_port->lock,flags);
		reg_blksize = readw(emmc_port->emmc_membase+EMMC_BLOCKSIZE_R);
		reg_blkcount = readw(emmc_port->emmc_membase+EMMC_BLOCKCOUNT_R);
		reg_argu = readl(emmc_port->emmc_membase+EMMC_ARGUMENT_R);

		trace_mmc_rtkemmc_legacy_cmd(reg_blksize, reg_blkcount, CMD_IDX_MASK(cmd_idx), reg_argu);

		rtkemmc_writew(cmd_idx,emmc_port->emmc_membase+EMMC_CMD_R);
		reg_cmdidx = readw(emmc_port->emmc_membase+EMMC_CMD_R);
		spin_unlock_irqrestore(&emmc_port->lock,flags);
		wait_for_completion(emmc_port->int_waiting);

		if(XFER_flag==1) {	//cmd with data
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R), 0x2, 0x2, __func__);
		}
		else{
			wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + EMMC_NORMAL_INT_STAT_R), 0x1, 0x1, __func__);
		}
		if (emmc_port->normal_interrupt & EMMC_ERR_INTERRUPT) {
			if (!ignore_log) {
				if (CMD_IDX_MASK(cmd_idx)!=MMC_GO_IDLE_STATE) {
					pr_err("0x98012004 EMMC BLOCK SIZE = 0x%x\n",reg_blksize);
					pr_err("0x98012006 EMMC BLOCK COUNT = 0x%08x\n", reg_blkcount);
					pr_err("0x98012008 EMMC CMDARG = 0x%08x\n", reg_argu);
					pr_err("0x9801200e EMMC CMD REG = 0x%08x\n", reg_cmdidx);
					print_err_reg(cmd_idx, emmc_port->normal_interrupt, emmc_port->error_interrupt);
					print_ip_desc(emmc_port);
					print_reg_info(emmc_port);
				}
			}
			else{ //in tuning, only print rintsts
#ifdef RTKEMMC_DEBUG
				pr_err("Tuning error case!!!\n");
#endif
			}

			err = RTK_FAIL;
		}
	}
	return err;
}

static void make_sg_des(struct sd_cmd_pkt *cmd_info, u32 p_des_base, struct rtkemmc_host *emmc_port,
			struct scatterlist *sg, u32 dma_nents)
{
	u32  blk_cnt;
	u32  blk_cnt2;
	u32  remain_blk_cnt;
	u32  tmp_val;
	u32* des_base = emmc_port->desc_vaddr ;
	u32  dma_leng = 0;
	u32  dma_addr;
	u32  i;
	unsigned int b1, b2;

#ifdef RTKEMMC_PHASE_TRACE
	trace_desc_counter = 0;
#endif
	for(i=0; i<dma_nents; i++,sg++) {
		dma_leng = sg_dma_len(sg);

		if(dma_leng<512) {		/*blk_cnt must be the multiple of 512(0x200)*/
			blk_cnt = 1;
		}
		else{
			blk_cnt  = dma_leng>>9;
		}

		remain_blk_cnt  = blk_cnt;
		dma_addr = sg_dma_address(sg);

		while(remain_blk_cnt) {
			/* setting des1; buffer size in byte */
                	if(remain_blk_cnt > EMMC_MAX_SCRIPT_BLK)
                        	blk_cnt2 = EMMC_MAX_SCRIPT_BLK;
                	else
                        	blk_cnt2 = remain_blk_cnt;

			//boundary check
			b1 = dma_addr / 0x8000000;		//this eMMC ip dma transfer has 128MB limitation
			b2 = (dma_addr+blk_cnt2*512) / 0x8000000;
			if(b1 != b2) {
				blk_cnt2 = (b2*0x8000000-dma_addr) / 512;
			}

			if(dma_leng<512) tmp_val = ((dma_leng)<<16)|0x21;
			else tmp_val = ((blk_cnt2&0x7f)<<25)|0x21;

			if((i==(dma_nents-1)) && (remain_blk_cnt == blk_cnt2))
				tmp_val |= 0x2;

			des_base[0] =  tmp_val;
			des_base[1] =  dma_addr;

			isb();
			sync(emmc_port);

			dma_addr = dma_addr+(blk_cnt2<<9);
			remain_blk_cnt -= blk_cnt2;
			des_base += 2;

			isb();
			sync(emmc_port);
#ifdef RTKEMMC_PHASE_TRACE
			trace_desc_counter += 2;
#endif
		}
	}

	wmb();
	sync(emmc_port);
}

static void make_ip_des(u32 dma_addr, u32 dma_length, u32 p_des_base, struct rtkemmc_host *emmc_port)
{
	u32  blk_cnt;
	u32  blk_cnt2;
	u32  remain_blk_cnt;
	u32  tmp_val;
	u32* des_base = emmc_port->desc_vaddr ;
	unsigned int b1, b2;

	isb();
	sync(emmc_port);

	MMCPRINTF("RTKEMMC: des_base = 0x%08x\n", des_base);
	//blk_cnt must be the multiple of 512(0x200)
	if(dma_length<512) {
		blk_cnt = 1;
	}
	else{
		blk_cnt  = dma_length>>9;
	}
	remain_blk_cnt  = blk_cnt;

	isb();
	sync(emmc_port);
#ifdef RTKEMMC_PHASE_TRACE
	trace_desc_counter = 0;
#endif

	while(remain_blk_cnt) {
		/* setting des1; buffer size in byte */
		if(remain_blk_cnt > EMMC_MAX_SCRIPT_BLK)
			blk_cnt2 = EMMC_MAX_SCRIPT_BLK;
		else
			blk_cnt2 = remain_blk_cnt;

		 //boundary check
		b1 = dma_addr / 0x8000000;              //this eMMC ip dma transfer has 128MB limitation
		b2 = (dma_addr+blk_cnt2*512) / 0x8000000;
		if(b1 != b2) {
			blk_cnt2 = (b2*0x8000000-dma_addr) / 512;
		}

		if(dma_length<512) tmp_val = ((dma_length)<<16)|0x21;
		else tmp_val = ((blk_cnt2&0x7f)<<25)|0x21;

		if(remain_blk_cnt == blk_cnt2)
			tmp_val |= 0x2;

		des_base[0] = tmp_val;
		des_base[1] = dma_addr;

		isb();
		sync(emmc_port);

		MMCPRINTF("%s - remain cnt : 0x%08x, desc[0]=0x%08x, desc[1]=0x%08x\n",
			__func__, remain_blk_cnt,des_base[0],des_base[1]);

		dma_addr = dma_addr+(blk_cnt2<<9);
		remain_blk_cnt -= blk_cnt2;
		des_base += 2;
		isb();
		sync(emmc_port);
#ifdef RTKEMMC_PHASE_TRACE
		trace_desc_counter +=2;
#endif
	}

	wmb();
	sync(emmc_port);
}

static void rtkemmc_read_rsp(struct rtkemmc_host *emmc_port,u32 *rsp, int reg_count)
{
	MMCPRINTF("rsp addr=0x%p; rsp_count=%u\n", rsp, reg_count);
	if ( reg_count==6 ){
		rsp[0] = rsp[1] = 0;
		rsp[0] = readl(emmc_port->emmc_membase + EMMC_RESP01_R);
		MMCPRINTF(KERN_INFO "rsp[0]=0x%08x, rsp[1]=0x%08x\n",rsp[0],rsp[1]);
	}else if(reg_count==17){
		/*1. UNSTUFF_BITS uses the reverse order as: const int __off = 3 - ((start) / 32);
		  2. be32_to_cpu is called in mmc_send_csd as csd[i] = be32_to_cpu(csd_tmp[i]);*/
		//in hank eMMC IP, we neeed to rearrange  response in 17 bytes case because they save 8-135 bit instead of 0-127 bit
		u32 rsp_tmp[4]={0};
		rsp_tmp[3] = readl(emmc_port->emmc_membase + EMMC_RESP01_R);
		rsp_tmp[2] = readl(emmc_port->emmc_membase + EMMC_RESP23_R);
		rsp_tmp[1] = readl(emmc_port->emmc_membase + EMMC_RESP45_R);
		rsp_tmp[0] = readl(emmc_port->emmc_membase + EMMC_RESP67_R);
		rsp[3] = (rsp_tmp[3]&0x00ffffff)<<8;
		rsp[2] = ((rsp_tmp[2]&0x00ffffff)<<8) | ((rsp_tmp[3]&0xff000000)>>24);
		rsp[1] = ((rsp_tmp[1]&0x00ffffff)<<8) | ((rsp_tmp[2]&0xff000000)>>24); 
		rsp[0] = ((rsp_tmp[0]&0x00ffffff)<<8) | ((rsp_tmp[1]&0xff000000)>>24);
		MMCPRINTF(KERN_INFO "rsp[0]=0x%08x, rsp[1]=0x%08x, rsp[2]=0x%08x, rsp[3]=0x%08x\n",rsp[0],rsp[1],rsp[2],rsp[3]);
	}
	else
		MMCPRINTF("rsp[0]=0x%08x\n",rsp[0]);
}

static int SD_SendCMDGetRSP(struct sd_cmd_pkt *cmd_info,int bIgnore)
{
	volatile u8 cmd_idx              = cmd_info->cmd->opcode;
	u32 *rsp                = (u32 *)&cmd_info->cmd->resp;
	struct rtkemmc_host *emmc_port = cmd_info->emmc_port;
	int err;//, retry_count=0;
	int rty_cnt =0;

//	wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + EMMC_PSTATE_REG), 0x3, 0x0, __func__);

SD_SendCMDGetRSP_Cmd_RETRY:
	rtkemmc_set_rspparam(emmc_port,cmd_info);   //for 1295
	if(rsp == NULL)
		BUG_ON(1);

	rtkemmc_writel(cmd_info->cmd->arg, emmc_port->emmc_membase + EMMC_ARGUMENT_R);
	isb();
	sync(emmc_port);

	rtkemmc_writew(0, emmc_port->emmc_membase + EMMC_XFER_MODE_R);

	emmc_port->cmd_opcode = cmd_idx;
	if (cmd_idx == MMC_SET_BLOCK_COUNT)
		emmc_port->rpmb_cmd = 1;
	else
		emmc_port->rpmb_cmd = 0;
	err = rtkemmc_wait_opt_end(emmc_port,((cmd_idx<<8)|cmd_info->cmd_para),bIgnore);
	if(err == RTK_SUCC){
		sync(emmc_port);
		rtkemmc_read_rsp(emmc_port,rsp, cmd_info->rsp_len);
		sync(emmc_port);
#if 0
		printk(KERN_INFO "%s: rsp[0]=0x%x, rsp[1]=0x%x, rsp[2]=0x%x, rsp[3]=0x%x\n",__func__, rsp[0], rsp[1], rsp[2], rsp[3]);
#endif
	}
	else {
		if(!bIgnore) {
			pr_err("SD_SendCMDGetRSP_Cmd error...\n");
			if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
				(EMMC_AUTO_CMD_ERR|EMMC_CMD_IDX_ERR|EMMC_CMD_END_BIT_ERR|EMMC_CMD_CRC_ERR|EMMC_CMD_TOUT_ERR))!=0){ //check cmd line
#ifdef RTKEMMC_DEBUG
				printk(KERN_INFO "CMD Line error occurs \n");
#endif
				rtkemmc_writeb(0x2, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
				wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x2<<24), 0x0, __func__); //wait for clear 0x2f bit 1
			}
			if((readw(emmc_port->emmc_membase + EMMC_ERROR_INT_STAT_R)&
				(EMMC_ADMA_ERR|EMMC_DATA_END_BIT_ERR|EMMC_DATA_CRC_ERR|EMMC_DATA_TOUT_ERR)) !=0){ //check data line
#ifdef RTKEMMC_DEBUG
				printk(KERN_INFO "DAT Line error occurs \n");
#endif
				rtkemmc_writeb(0x4, emmc_port->emmc_membase + EMMC_SW_RST_R); //Perform a software reset
				wait_done_timeout(emmc_port, (u32*)(emmc_port->emmc_membase + 0x2c), (0x4<<24), 0x0, __func__); //wait for clear 0x2f bit 2
			}
			if((rty_cnt++)<10)
				goto SD_SendCMDGetRSP_Cmd_RETRY;
		}
	}
	return err;
}

static int SD_Stream(struct sd_cmd_pkt *cmd_info, unsigned int bIgnore)
{
	u8 cmd_idx              = cmd_info->cmd->opcode;
	u32 *rsp                = (u32 *)&cmd_info->cmd->resp;
	u16 block_count         = cmd_info->block_count;
	u8 *data              = cmd_info->dma_buffer;

	int err = 0;
	int read_flag=1;
	int mul_blk_flag=0;
	int auto_stop_flag=0;
	int rty_cnt=0;

	struct scatterlist *sg;
	u32 dma_nents = 0;
	u32 dir = 0;

	struct mmc_host *host;
	struct rtkemmc_host *emmc_port = cmd_info->emmc_port;
	if(emmc_port==NULL) {
		pr_err("emmc_port == NULL\n");
		BUG_ON(1);
	}
	host = emmc_port->mmc;

	if(rsp == NULL)
		BUG_ON(1);

	if(cmd_info->data)
	{       /*command issued from MMC framework case*/
		cmd_info->data->bytes_xfered=0;

		if(cmd_info->data->flags & MMC_DATA_READ)
			dir = DMA_FROM_DEVICE;
		else
			dir = DMA_TO_DEVICE;

		dma_nents = dma_map_sg( mmc_dev(host), cmd_info->data->sg, cmd_info->data->sg_len, dir);
		sg = cmd_info->data->sg;

		make_sg_des(cmd_info, emmc_port->desc_paddr, emmc_port, sg, dma_nents);
        }
	else if(data)
	{
                /*command issued by Realtek host driver*/
		if(cmd_idx == MMC_SEND_TUNING_BLOCK_HS200)
			make_ip_des(emmc_port->dma_paddr, 0x80, emmc_port->desc_paddr, emmc_port);
		else if(cmd_idx==MMC_SEND_WRITE_PROT)
			make_ip_des(emmc_port->dma_paddr, 0x4, emmc_port->desc_paddr, emmc_port);
		else if(cmd_idx==MMC_SEND_WRITE_PROT_TYPE)
			make_ip_des(emmc_port->dma_paddr, 0x8, emmc_port->desc_paddr, emmc_port);
		else
			make_ip_des(emmc_port->dma_paddr, block_count<<9, emmc_port->desc_paddr, emmc_port);
	}
	else
		BUG_ON(1);

SD_Stream_Cmd_RETRY:
	rtkemmc_set_rspparam(emmc_port,cmd_info);   //for 119x

	/*************************************************************************/
	rtkemmc_writel(readl(emmc_port->iso_blk_membase + EMMC_SWC_SEL)|0x10, emmc_port->iso_blk_membase + EMMC_SWC_SEL);
	rtkemmc_writel(readl(emmc_port->iso_blk_membase + EMMC_SWC_SEL1)&0xffffffef, emmc_port->iso_blk_membase + EMMC_SWC_SEL1);
	rtkemmc_writel(readl(emmc_port->iso_blk_membase + EMMC_SWC_SEL2)|0x10, emmc_port->iso_blk_membase + EMMC_SWC_SEL2);
	rtkemmc_writel(readl(emmc_port->iso_blk_membase + EMMC_SWC_SEL3)&0xffffffef, emmc_port->iso_blk_membase + EMMC_SWC_SEL3);
	rtkemmc_writel(0, emmc_port->emmc_membase + EMMC_CP);
	/*************************************************************************/
	rtkemmc_writew(block_count, emmc_port->emmc_membase + EMMC_BLOCKCOUNT_R);
	rtkemmc_writel(emmc_port->desc_paddr, emmc_port->emmc_membase + EMMC_ADMA_SA_LOW_R);

	rtkemmc_writel(cmd_info->cmd->arg, emmc_port->emmc_membase + EMMC_ARGUMENT_R);

	if(cmd_idx==MMC_WRITE_BLOCK || cmd_idx==MMC_WRITE_MULTIPLE_BLOCK || cmd_idx==MMC_LOCK_UNLOCK
		||cmd_idx==47 || cmd_idx==49 || (cmd_idx==MMC_GEN_CMD && cmd_info->cmd->arg==0))
		read_flag=0;

	if(cmd_idx==MMC_WRITE_MULTIPLE_BLOCK || cmd_idx==MMC_READ_MULTIPLE_BLOCK) {
		mul_blk_flag=1;
		auto_stop_flag=1;
	}

	rtkemmc_writew((mul_blk_flag<<EMMC_MULTI_BLK_SEL)|
		       (read_flag<<EMMC_DATA_XFER_DIR)|
		       (auto_stop_flag<<EMMC_AUTO_CMD_ENABLE)|
		        EMMC_BLOCK_COUNT_ENABLE|
			EMMC_DMA_ENABLE,
			emmc_port->emmc_membase + EMMC_XFER_MODE_R);
	isb();
	sync(emmc_port);

	emmc_port->cmd_opcode = cmd_idx;

	rtkemmc_get_cmd_timeout(cmd_info);
	isb();
	sync(emmc_port);
	err = rtkemmc_wait_opt_end(emmc_port,((cmd_idx<<8)|cmd_info->cmd_para),bIgnore);
	isb();
	sync(emmc_port);

	if(err == RTK_SUCC) {
		rtkemmc_read_rsp(emmc_port,rsp, cmd_info->rsp_len);
		if(cmd_info->data)
                        cmd_info->data->bytes_xfered += (block_count << 9);
	}
	else {
		if(!bIgnore) {
			pr_err("SD_Stream_Cmd error...\n");
			error_handling(emmc_port);
			if((rty_cnt++)<10)
				goto SD_Stream_Cmd_RETRY;
		}
	}

	if(cmd_info->data)
                dma_unmap_sg(mmc_dev(host), cmd_info->data->sg, cmd_info->data->sg_len, dir);

	return err;
}

static void rtkemmc_send_command(struct rtkemmc_host *emmc_port,
				 struct mmc_command *cmd,
				 u8 Ignore)
{
	struct sd_cmd_pkt cmd_info;
	int rc = 0;

	memset(&cmd_info, 0, sizeof(struct sd_cmd_pkt));

	if ( !emmc_port || !cmd ){
		pr_err("%s: emmc_port or cmd is null\n",DRIVER_NAME);
		return ;
	}

	cmd_info.cmd    = cmd;
	cmd_info.emmc_port = emmc_port;
	cmd_info.dma_buffer = NULL;

	if (cmd->data) {
		cmd_info.data = cmd->data;
		cmd_info.block_count =  cmd_info.data->blocks;
		cmd_info.byte_count = BYTE_CNT;
		rc = SD_Stream(&cmd_info, Ignore);
	}
	else {
		rc = SD_SendCMDGetRSP(&cmd_info, Ignore);
	}

	if (rc){
		cmd->error = -MMC_BLK_CMD_ERR;
	}
}

static void read_tuning_parameter(struct rtkemmc_host *emmc_port)
{
	int hs400_data[128]={0};        //4 bytes header, 33aa, 4 bytes for TX, 4 bytes for RX, 4 bytes for dqs
	int chk_sum1 = 0, chk_sum2 = 0;
	unsigned int *buf;
	size_t buf_size;

	buf = nvmem_cell_read(emmc_port->cell, &buf_size);

	rtkemmc_send_cmd18(emmc_port, 512, emmc_port->emmc_tuning_addr/512+1024, 0);       //we put the hs400 parameter data before tuning blk addr 1 block
	memcpy(hs400_data, emmc_port->dma_vaddr, 512);

	printk(KERN_ERR "read dqs_data from emmc: hs400_version=0x%x, hs400_TX=0x%x, hs400_RX=0x%x, hs400_dqs=0x%x, cmd_dly_tap=0x%x\n",
		hs400_data[0], hs400_data[1], hs400_data[2], hs400_data[3], hs400_data[4]);
		printk(KERN_ERR "UUID expt: [0]=0x%x, [1]=0x%x, [2]=0x%x\n", hs400_data[6], hs400_data[7], hs400_data[8]);
		printk(KERN_ERR "UUID real: [0]=0x%x, [1]=0x%x, [2]=0x%x\n", buf[0], buf[1], buf[2]);

	chk_sum1 = hs400_data[0]+hs400_data[1]*2+hs400_data[2]*3+hs400_data[3]*4+hs400_data[4]*5;
	chk_sum2 = HS400_VERSION+hs400_data[1]*2+hs400_data[2]*3+hs400_data[3]*4+hs400_data[4]*5;

	if(chk_sum1==chk_sum2 && hs400_data[5]==chk_sum2 &&
		buf[0]==hs400_data[6] && buf[1]==hs400_data[7] &&
		buf[2]==hs400_data[8]) {     //inherit from emmc saved data instead of bootcode
			emmc_port->dqs_tuning=0;
			emmc_port->tx_tuning = 0;
			emmc_port->rx_tuning = 0;

			HS200_TX = emmc_port->tx_phase = hs400_data[1];
			HS200_RX = emmc_port->rx_phase = hs400_data[2];
			emmc_port->dqs = hs400_data[3];
			emmc_port->cmd_dly_tap = hs400_data[4];

			if(emmc_port->tx_user_defined) {        //if we set user defined tx and rx value, then we won't use the bootcode reference value, always the first priority
				printk(KERN_ERR "tx_user_defined has been set, we still use user define value for usage\n");
				HS200_TX = emmc_port->tx_phase = emmc_port->tx_reference_phase;
			}
			if(emmc_port->rx_user_defined) {
				printk(KERN_ERR "rx_user_defined has been set, we still use user define value for usage\n");
				HS200_RX = emmc_port->rx_phase = emmc_port->rx_reference_phase;
			}
	}
	kfree(buf);
}

static int rtkemmc_hs400_prepare_ddr(struct mmc_host *host)
{
	if(!host->card)
		pr_err("%s: host->card==NULL\n", __func__);
	/* in realtek chip, some emmc hs400 mode needs to strengthen the device ability */
	else if(host->card->ext_csd.raw_driver_strength & 0x2)
		host->card->drive_strength = 0x1;
	printk(KERN_ERR "%s: card->ext_csd.raw_driver_strength=%x\n",
		__func__, host->card->ext_csd.raw_driver_strength);

	return 0;
}

static void rtkemmc_init_card(struct mmc_host *host, struct mmc_card *card)
{
	host->card = card;
}

static void rtkemmc_request(struct mmc_host *host, struct mmc_request *mrq)
{
	struct rtkemmc_host *emmc_port;
	int ret = 0;

	emmc_port = mmc_priv(host);
	BUG_ON(emmc_port->mrq != NULL);

	down_write(&emmc_port->cr_rw_sem);
	if(emmc_port->cmdq==1)
	{
		if(host->cqe_on==false && emmc_port->cq_host->activated==true
			&& emmc_port->switch_partition==0)
			cqhci_deactivate(host);

		if(mrq->cmd->opcode==MMC_SWITCH && mrq->cmd->arg==CMDQ_DISABLED)
			emmc_port->switch_partition = 1;

		/*	we do not need to disable cmdq if it is rpmb request
			because rpmb has been changed to rpmb partition in block.c
			Also, we do not need to disable cmdq if this command is disable/enable cmdq*/
		if(host->card && host->card->ext_csd.cmdq_en==1
			&& emmc_port->switch_partition==0) {
			ret = rtkemmc_blk_cmdq_switch(host->card, false);
			if(mrq->cmd->opcode==MMC_SLEEP_AWAKE ||
			   (mrq->cmd->opcode==MMC_SELECT_CARD && mrq->cmd->arg==0x0) ||
			   (mrq->cmd->opcode==MMC_SWITCH && mrq->cmd->arg==0x03220301)) {
				emmc_port->cmdq_reenable=0;
			}
			else
				emmc_port->cmdq_reenable=1;
			if (ret) {
				pr_err("%s: disable cmdq failed !\n", __func__);
			}
		}

		if(mrq->cmd->opcode==MMC_SWITCH && mrq->cmd->arg==CMDQ_ENABLED)
			emmc_port->switch_partition = 0;
	}

	//emmc_port->cmdq_reenable=1 means curretnly we run under cmdq mode, but disable for the time being
	if(mrq->cmd->opcode==MMC_SEND_EXT_CSD && emmc_port->cmdq_reenable==0 && !emmc_port->hs400_force_tuning)
	{
		//====we add the following program becasue we need to read HS400 parameter in specific emmc block in SDR50 mode if exists====
		//hs400 mode case, we need to eliminate the tuning process, so we read the dqs, phase data from offset that bootcode provided
		if((host->caps2 & MMC_CAP2_HS400_1_8V) && emmc_port->dqs_tuning==1 && emmc_port->emmc_tuning_addr!=0)    //bootcode doesn't do the hs400 tuning
		{
			read_tuning_parameter(emmc_port);
		}
	}

	emmc_port->mrq = mrq;
	if(emmc_port->time_setting==1 && mrq->cmd->opcode==MMC_SEND_STATUS) {
		/*
		we skip this cmd 13 because that might be a cmd CRC error
		after the timing setting and before the phase setting
		User should avoid sending command when the eMMC is unstable.
		*/
		goto skip;
	}
	if(emmc_port && mrq->sbc)
		rtkemmc_send_command(emmc_port, mrq->sbc, 0);

	if(emmc_port && mrq->cmd)
		rtkemmc_send_command(emmc_port, mrq->cmd, 0);
skip:
	emmc_port->time_setting = 0;

	rtkemmc_req_end_tasklet((unsigned long)emmc_port);

	if(emmc_port->cmdq==1 && emmc_port->cmdq_reenable==1) {
		if(host->card && host->card->ext_csd.cmdq_en==0) {
			ret = rtkemmc_blk_cmdq_switch(host->card, true);
			emmc_port->cmdq_reenable=0;
			if (ret) {
				pr_err("%s: switch cmdq failed !\n", __func__);
			}
		}
	}

	up_write(&emmc_port->cr_rw_sem);
}

static int rtkemmc_sysfs_cmdq_disable(struct rtkemmc_host *emmc_port, int *cmdq_disable)
{
	struct mmc_host *host = emmc_port->mmc;
	int ret=0;

	if(emmc_port->cmdq==1)
	{
		if(host->cqe_on==true)
			host->cqe_ops->cqe_off(host);

		if(emmc_port->cq_host->activated==true)
			cqhci_deactivate(host);

		if(host->card && host->card->ext_csd.cmdq_en==1) {
			ret = rtkemmc_blk_cmdq_switch(host->card, false);
			printk(KERN_ERR "disable command queue mode...,DBR=0x%x, TCN=0x%x\n",
				readl(emmc_port->cq_host->mmio+CQHCI_TDBR), readl(emmc_port->cq_host->mmio+CQHCI_TCN));
			(*cmdq_disable)=1;
                        if (ret) {
                                pr_err("disable cmdq failed !!!\n");
                        }
                }
        }

	return ret;
}

static int rtkemmc_sysfs_partition_pre(struct rtkemmc_host *emmc_port)
{
	struct mmc_host *host = emmc_port->mmc;
	u8 part_config = host->card->ext_csd.part_config;
	int ret=0;

	if((host->card->ext_csd.part_config&0x7)!=0) {  //current partition is not uda
		printk(KERN_ERR "switch to uda partition...\n");
		part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
		part_config |= 0x0;     //uda partition
		//switch to uda partition because of tuning process
		ret = rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_PART_CONFIG,
			part_config, emmc_port->mmc->card->ext_csd.generic_cmd6_time);
	}

        return ret;
}

static int rtkemmc_sysfs_cmdq_enable(struct rtkemmc_host *emmc_port, int cmdq_disable)
{
	struct mmc_host *host = emmc_port->mmc;
	int ret=0;

	if(emmc_port->cmdq==1) {
		if(cmdq_disable==1 && host->card && host->card->ext_csd.cmdq_en==0) {
			printk(KERN_ERR "re-enable command queue mode...\n");
			ret = rtkemmc_blk_cmdq_switch(host->card, true);
			if (ret) {
                                pr_err("enable cmdq failed !!!\n");
			}
			rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		}
	}

	return ret;
}

static int rtkemmc_sysfs_partition_post(struct rtkemmc_host *emmc_port)
{
	struct mmc_host *host = emmc_port->mmc;
	int ret=0;

	if((host->card->ext_csd.part_config&0x7)!=0) {  //current partition is not uda
		printk(KERN_ERR "switch back to original partition...\n");
		ret = rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_PART_CONFIG,
			host->card->ext_csd.part_config, emmc_port->mmc->card->ext_csd.generic_cmd6_time);
	}

	return ret;
}

static ssize_t emmc_info_dev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mmc_host * host = dev_get_drvdata(dev);
	struct rtkemmc_host *emmc_port = mmc_priv(host);

	printk(KERN_INFO "%s(%u)\n",__func__,__LINE__);

	return sprintf(buf, "EMMC SYS_PLL_EMMC1=0x%08x SYS_PLL_EMMC2=0x%08x \nSYS_PLL_EMMC3=0x%08x SYS_PLL_EMMC4=0x%08x HOST_CONTROL2_REG=0x%08x\n \
				PRESENT_STATE_REG=0x%08x  HOST CONTROL1 REG=0x%08x TRANSFER_MODE_REG=0x%08x \n EMMC_CKGEN_CTL=0x%08x EMMC_DQS_CTRL1=0x%08x \n",
				readl(emmc_port->crt_membase + SYS_PLL_EMMC1),
				readl(emmc_port->crt_membase + SYS_PLL_EMMC2),
				readl(emmc_port->crt_membase + SYS_PLL_EMMC3),
				readl(emmc_port->crt_membase + SYS_PLL_EMMC4),
				readw(emmc_port->emmc_membase+EMMC_HOST_CTRL2_R),
				readl(emmc_port->emmc_membase+EMMC_PSTATE_REG),
				readb(emmc_port->emmc_membase+EMMC_HOST_CTRL1_R),
				readw(emmc_port->emmc_membase+EMMC_XFER_MODE_R),
				readl(emmc_port->emmc_membase+EMMC_CKGEN_CTL),
				readl(emmc_port->emmc_membase+EMMC_DQS_CTRL1));

}

static ssize_t emmc_info_dev_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	printk(KERN_ERR "%s(%u)Nothing to do\n",__func__,__LINE__);

	/* return value must be equare or big then "count" to finish this attribute */
	return count;
}
DEVICE_ATTR(emmc_info, S_IRUGO | S_IWUSR,
		emmc_info_dev_show,emmc_info_dev_store);

static ssize_t
tuning_info_dev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct rtkemmc_host *emmc_port = mmc_priv(host);
	ssize_t size;
	int cmdq_disable=0;
	int ret = 0;

	down_write(&emmc_port->cr_rw_sem);

	if(emmc_port->cmdq==1)
		mmc_get_card(host->card, NULL);

	ret = rtkemmc_sysfs_cmdq_disable(emmc_port, &cmdq_disable);
	if(ret!=0)
		goto out;
	ret = rtkemmc_sysfs_partition_pre(emmc_port);
	if(ret!=0)
		goto out;

	emmc_port->retune=1;
	if(emmc_port->mmc->caps2 & MMC_CAP2_HS400_1_8V) {
		int dqs_temp = emmc_port->dqs_tuning;
		int tx_temp = emmc_port->tx_tuning;
		int rx_temp = emmc_port->rx_tuning;

		emmc_port->dqs_tuning=1;
		emmc_port->tx_tuning = 1;
		emmc_port->rx_tuning = 1;

		rtkemmc_dqs_tuning(emmc_port->mmc);

		emmc_port->dqs_tuning=dqs_temp;
		emmc_port->tx_tuning = tx_temp;
		emmc_port->rx_tuning = rx_temp;

		size = sprintf(buf, "The eMMC hs400 tuning finished !!!\n");
	}
	else if(emmc_port->mmc->caps2 & MMC_CAP2_HS200_1_8V_SDR) {
		int tx_temp = emmc_port->tx_tuning;
                int rx_temp = emmc_port->rx_tuning;

		emmc_port->tx_tuning = 1;
		emmc_port->rx_tuning = 1;

		mmc_Tuning_HS200(emmc_port);

		emmc_port->tx_tuning = tx_temp;
		emmc_port->rx_tuning = rx_temp;

		size = sprintf(buf, "The eMMC hs200 tuning finished !!!\n");
	}
	else {
		size = sprintf(buf, "This function only supports hs400 or hs200 mode !!!\n");
	}
	emmc_port->retune=0;

	ret = rtkemmc_sysfs_partition_post(emmc_port);
	if(ret)
		goto out;
	ret = rtkemmc_sysfs_cmdq_enable(emmc_port, cmdq_disable);
out:
	if(ret!=0)
		size = sprintf(buf, "The eMMC tuning is not finsihed !!!\n");

	if(emmc_port->cmdq==1)
		mmc_put_card(host->card, NULL);

	up_write(&emmc_port->cr_rw_sem);

	return size;
}

static ssize_t tuning_info_dev_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	printk(KERN_ERR "%s(%u)Nothing to do\n",__func__,__LINE__);

	/* return value must be equare or big then "count" to finish this attribute */
	return count;
}
DEVICE_ATTR(tuning_info, S_IRUGO | S_IWUSR,
		tuning_info_dev_show, tuning_info_dev_store);

void euda_gpp_setting(struct mmc_host *host, unsigned long size[], char type[], int gpp_num, unsigned long euda_start_addr, unsigned long euda_size)
{
	struct rtkemmc_host *emmc_port = mmc_priv(host);

	unsigned long mul[4]={0};
	unsigned long temp=0;
	int idx=0;
	int enhance_attr=0;
	int cmdq_disable=0;
	int ret;

	for(idx=0; idx<4; idx++) {
		if(size[idx]==0) break;
		printk(KERN_ERR "gpp%d: size=%lu, type=%c\n", idx, size[idx], type[idx]);
	}
	printk(KERN_ERR "gpp_num=%d, euda_start_addr=0x%lx, euda_size=%ld\n", gpp_num, euda_start_addr, euda_size);

	down_write(&emmc_port->cr_rw_sem);

	if(emmc_port->cmdq==1)
		mmc_get_card(host->card, NULL);

	ret = rtkemmc_sysfs_cmdq_disable(emmc_port, &cmdq_disable);
	if(ret!=0)
		goto out;

	rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_SANITIZE_START, 0x1, host->card->ext_csd.generic_cmd6_time);
	rtkemmc_wait_status(host->card,STATE_TRAN,0,0);

	//set euda
	if(euda_size !=0) {
		enhance_attr |= (1<<0);
		temp = euda_size;
		mul[2] = (temp >> 16);
		temp = temp - (mul[2]<<16);
		mul[1] = (temp>>8);
		temp = temp - (mul[1]<<8);
		mul[0] = temp;

		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 140, mul[0], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 141, mul[1], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 142, mul[2], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);

		temp = euda_start_addr;
		mul[3] = (temp >> 24);
		temp = temp - (mul[3]<<24);
		mul[2] = (temp >> 16);
		temp = temp - (mul[2]<<16);
		mul[1] = (temp>>8);
		temp = temp - (mul[1]<<8);
		mul[0] = temp;

		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 136, mul[0], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 137, mul[1], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 138, mul[2], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, 139, mul[3], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
	}
	//set gpp
	for(idx=0; idx<gpp_num; idx++) {
		if(type[idx]=='s' || type[idx]=='S')
			enhance_attr |= (1<<(idx+1));

		temp = size[idx];
		mul[2] = (temp >> 16);
		temp = temp - (mul[2]<<16);
		mul[1] = (temp>>8);
		temp = temp - (mul[1]<<8);
		mul[0] = temp;

		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_GP_SIZE_MULT+idx*3, mul[0], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_GP_SIZE_MULT+idx*3+1, mul[1], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
		rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_GP_SIZE_MULT+idx*3+2, mul[2], host->card->ext_csd.generic_cmd6_time);
		rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
	}
	printk(KERN_ERR "enhance_attr=0x%x\n", enhance_attr);
	rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_PARTITION_ATTRIBUTE, enhance_attr, host->card->ext_csd.generic_cmd6_time);
	rtkemmc_wait_status(host->card,STATE_TRAN,0,0);
	rtkemmc_switch(emmc_port, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_PARTITION_SETTING_COMPLETED, 0x1, host->card->ext_csd.generic_cmd6_time);
	rtkemmc_wait_status(host->card,STATE_TRAN,0,0);

	ret = rtkemmc_sysfs_cmdq_enable(emmc_port, cmdq_disable);
out:
	if(emmc_port->cmdq==1)
		mmc_put_card(host->card, NULL);

	up_write(&emmc_port->cr_rw_sem);
}
EXPORT_SYMBOL(euda_gpp_setting);

static ssize_t gpp_info_dev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mmc_host * host = dev_get_drvdata(dev);
	struct rtkemmc_host *emmc_port = mmc_priv(host);

	printk(KERN_ERR "the gpp partition based_unit_size is  hc_erase_grp_sz*hc_wp_grp_sz*512 Kbytes=%d Kbytes\n",
			emmc_port->mmc->card->ext_csd.raw_hc_erase_grp_size*emmc_port->mmc->card->ext_csd.raw_hc_erase_gap_size*512);
	printk(KERN_ERR "each gpp partition size = GP_X_2 * 2^16 + GP_X_1 * 2^8 + GP_X_0 * 2^0\n");
	printk(KERN_ERR "X is from 1 to the number of gpp partitions\n");
	printk(KERN_ERR "Ex. echo 8,M,4,S,0,0x80000,20 > gpp_info means to set gpp 1 8*based_unit_size and the type is MLC\n");
	printk(KERN_ERR "gpp 2 4*based_unit_size and the type is SLC\n");
	printk(KERN_ERR "set euda where the start address is 0x80000 and the size is 20*based_unit_size\n");
	printk(KERN_ERR "emmc_port->mmc->card->ext_csd.raw_hc_erase_grp_size*emmc_port->mmc->card->ext_csd.raw_hc_erase_gap_size*512\n");

        return sprintf(buf, "hc_erase_grp_sz=0x%x, hc_wp_grp_sz=0x%x, EXT_CSD_PARTITION_SUPPORT=0x%x\n",
				emmc_port->mmc->card->ext_csd.raw_hc_erase_grp_size,
				emmc_port->mmc->card->ext_csd.raw_hc_erase_gap_size,
				emmc_port->mmc->card->ext_csd.raw_partition_support);

}

static ssize_t gpp_info_dev_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char buffer[200];
	char sep[8][20];
	unsigned long size[4]={0,0,0,0};
	char type[4];
	int para_num=0;
	int gpp_num=0;
	char* const delim = ",";
	char *token, *cur = buffer;
	unsigned long euda_start_addr = 0;
	unsigned long euda_size = 0;

	struct mmc_host * host = dev_get_drvdata(dev);
	struct rtkemmc_host *emmc_port = mmc_priv(host);

	if(!(emmc_port->mmc->card->ext_csd.raw_partition_support & EXT_CSD_PART_SUPPORT_PART_EN))
		return count;

	sscanf(buf, "%s", buffer);

	while ((token = strsep(&cur, delim))) {
		if(para_num==9) break;
		if(!strcmp(token,"0")) {
			token = strsep(&cur, delim);
			if(token == NULL) {
				euda_size = 0;
				printk(KERN_ERR "no euda start_addr: euda_start_addr=0x%lx, euda_size=%lu\n", euda_start_addr, euda_size);
				break;
			}

			euda_start_addr = simple_strtoul(token, NULL, 16);
			token = strsep(&cur, delim);
			if(token == NULL) {
				euda_size = 0;
				printk(KERN_ERR "no euda size: euda_start_addr=0x%lx, euda_size=%lu\n", euda_start_addr, euda_size);
				break;
			}
			euda_size = simple_strtoul(token, NULL, 10);
			break;
		}
		strcpy(sep[para_num], token);
		++para_num;
	}

	for(gpp_num=0;gpp_num<(para_num/2);gpp_num++) {
		size[gpp_num] = simple_strtoul(sep[gpp_num*2], NULL, 10);
		type[gpp_num] = sep[gpp_num*2+1][0];
	}

	euda_gpp_setting(emmc_port->mmc, size, type, para_num/2, euda_start_addr, euda_size);

	return count;
}
DEVICE_ATTR(gpp_info, S_IRUGO | S_IWUSR,
                gpp_info_dev_show, gpp_info_dev_store);

static ssize_t protect_region_setting_dev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "echo Q > protect_region_setting to show all eMMC protect group regions,    \
		echo C > protect_region_setting to clear all eMMC protect group region\n");
}

static ssize_t protect_region_setting_dev_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_host * host = dev_get_drvdata(dev);
	struct rtkemmc_host *emmc_port = mmc_priv(host);
	unsigned long args=0;
	unsigned int wpg_unit=0;
	int cmdq_disable=0;
	int ret = 0;

	down_write(&emmc_port->cr_rw_sem);

	if(emmc_port->cmdq==1)
		mmc_get_card(host->card, NULL);

	ret = rtkemmc_sysfs_cmdq_disable(emmc_port, &cmdq_disable);
	if(ret!=0)
		goto out;
	ret = rtkemmc_sysfs_partition_pre(emmc_port);
	if(ret!=0)
		goto out;

	wpg_unit= emmc_port->mmc->card->ext_csd.raw_hc_erase_grp_size*emmc_port->mmc->card->ext_csd.raw_hc_erase_gap_size*1024;
	if(buf[0]=='Q') {
		//wpg_unit is write protect grpoup size (sector unit)
		printk(KERN_ERR "curret eMMC protect region status:\n");
		for(args=0; args<emmc_port->mmc->card->ext_csd.sectors;args+=(wpg_unit*0x20)) {
			rtkemmc_query_protect_cmd(emmc_port, args, MMC_SEND_WRITE_PROT);
			rtkemmc_wait_status(emmc_port->mmc->card,STATE_TRAN,0,0);
		}
	}
	else if(buf[0]=='C') {
		printk(KERN_ERR "clear all eMMC protect group regions...\n");
		for(args=0; args<emmc_port->mmc->card->ext_csd.sectors;args+=wpg_unit) {
			rtkemmc_write_protect_cmd(emmc_port, args, 0);
			rtkemmc_wait_status(emmc_port->mmc->card,STATE_TRAN,0,0);
		}
	}

	ret = rtkemmc_sysfs_partition_post(emmc_port);
	if(ret)
		goto out;
	ret = rtkemmc_sysfs_cmdq_enable(emmc_port, cmdq_disable);
out:
	if(emmc_port->cmdq==1)
		mmc_put_card(host->card, NULL);

	up_write(&emmc_port->cr_rw_sem);

	return count;
}

DEVICE_ATTR(protect_region_setting, S_IRUGO | S_IWUSR,
		protect_region_setting_dev_show, protect_region_setting_dev_store);

static int count_class_dev(struct device *dev, const void *data)
{
	int *p = (void *)data;

	*p += 1;
	return 0;
}

static void rtkemmc_wait_block_dev_ready(struct mmc_host *mmc)
{
	int retry = 500;
	int cb = 0, ca = 0;
	ktime_t tb, ta;

	tb = ktime_get();
	class_find_device(&block_class, NULL, &cb, count_class_dev);

	while (--retry > 0) {
		if (mmc->card && dev_get_drvdata(&mmc->card->dev) != NULL) {
			break;
		}
		msleep(10);
	}

	class_find_device(&block_class, NULL, &ca, count_class_dev);
	ta = ktime_get();
	printk(KERN_INFO "%s: retry_left=%d, block_dev=[%d -> %d], time=%dms\n",
		mmc_hostname(mmc), retry, cb, ca, (int)ktime_to_ms(ktime_sub(ta, tb)));

}

static int rtkemmc_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc = NULL;
	struct rtkemmc_host *emmc_port = NULL;
	int ret = 0;
	int att_err;
	const u32 *prop;
	int size;
	struct device_node *emmc_node = NULL;
	int i;
	int counter=0;
	emmc_node = pdev->dev.of_node;

	if (!emmc_node)
		pr_err("%s : No emmc of_node found\n",DRIVER_NAME);
	else
		printk(KERN_ERR "%s : emmc of_node found\n",DRIVER_NAME);

	mmc = mmc_alloc_host(sizeof(struct rtkemmc_host), &pdev->dev);

	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	emmc_port = mmc_priv(mmc);
	memset(emmc_port, 0, sizeof(struct rtkemmc_host));
	emmc_port->mmc = mmc;
	emmc_port->dev = &pdev->dev;

	att_err = device_create_file(&pdev->dev, &dev_attr_emmc_info);
	att_err = device_create_file(&pdev->dev, &dev_attr_tuning_info);
	att_err = device_create_file(&pdev->dev, &dev_attr_gpp_info);
	att_err = device_create_file(&pdev->dev, &dev_attr_protect_region_setting);

	emmc_port->irq = irq_of_parse_and_map(emmc_node, 0);
	if (emmc_port->irq <= 0) {
                pr_err("%s : fail to parse of irq.\n",DRIVER_NAME);
                return -ENXIO;
	}

	emmc_port->emmc_membase = of_iomap(emmc_node, 0);
	emmc_port->crt_membase = of_iomap(emmc_node, 1);
	emmc_port->sb2_membase = of_iomap(emmc_node, 2);
	emmc_port->mux_mis_membase = of_iomap(emmc_node, 3);
	emmc_port->iso_blk_membase = of_iomap(emmc_node, 4);
	emmc_port->m2tmx_membase = of_iomap(emmc_node, 5);

	if (!emmc_port->emmc_membase || !emmc_port->crt_membase ||
		!emmc_port->sb2_membase || !emmc_port->mux_mis_membase ||
		!emmc_port->iso_blk_membase || !emmc_port->m2tmx_membase) {
		pr_err("Realtek EMMC Controller Driver probe fail - nomem !!!\n\n");
		ret = -ENOMEM;
		goto out;
	}

#if defined(CONFIG_MMC_RTK_EMMC_PON)
	emmc_port->norst_membase = of_iomap(emmc_node, 7);

	emmc_port->emmc_pon_gpio = devm_gpiod_get(&pdev->dev, "emmc-pon", GPIOD_OUT_HIGH);
	if (IS_ERR(emmc_port->emmc_pon_gpio)) {
		pr_err("%s: can't request emmc_pon_gpio %d\n", __func__, desc_to_gpio(emmc_port->emmc_pon_gpio));
	}

	emmc_port->emmc_pon_toggle_gpio = devm_gpiod_get(&pdev->dev, "emmc-pon-toggle", GPIOD_OUT_HIGH);
	if (IS_ERR(emmc_port->emmc_pon_toggle_gpio)) {
		pr_err("%s: can't request emmc_pon_toggle_gpio %d\n", __func__, desc_to_gpio(emmc_port->emmc_pon_toggle_gpio));
	}
#endif

	emmc_port->dma_vaddr = NULL;
	emmc_port->desc_vaddr = NULL;
	emmc_port->tx_phase = EMMC_DEFAULT_PHASE_VALUE;
	emmc_port->rx_phase = EMMC_DEFAULT_PHASE_VALUE;
	emmc_port->dqs = EMMC_DEFAULT_PHASE_VALUE;
	emmc_port->cmd_dly_tap = EMMC_DEFAULT_PHASE_VALUE;
	emmc_port->cmdq_reenable=0;
	emmc_port->retune=0;
	emmc_port->switch_partition = 0;
	emmc_port->time_setting = 0;

	emmc_port->cell = nvmem_cell_get(&pdev->dev, "uuid"); // get cell
	if (IS_ERR(emmc_port->cell)) {
		pr_err("cannot get the uuid info !\n");
	}
#if 0
	prop = of_get_property(pdev->dev.of_node, "interrupts", &size);
	if (prop) {
		emmc_port->irq_num = of_read_number(prop, 2);
		printk(KERN_INFO "[%s] get interrupts irq number : %d \n",__func__, emmc_port->irq_num);
	}else {
		printk(KERN_ERR "[%s] get interrupts irq number error !!\n",__func__);
	}
#endif
	prop = of_get_property(pdev->dev.of_node, "speed-step", &size);
	if (prop) {
		emmc_port->speed_step = of_read_number(prop, 1);
		printk(KERN_INFO "[%s] get speed-step : %d \n",__func__, emmc_port->speed_step);
	} else {
		pr_err("[%s] get speed-step error and use default speed-step 0.\n",__func__);
		emmc_port->speed_step = 0;
	}

	switch(emmc_port->speed_step) {
	case 0:
		prop = of_get_property(pdev->dev.of_node, "pddrive_nf_s0", &size);
		break;
	case 1:
		prop = of_get_property(pdev->dev.of_node, "pddrive_nf_s1", &size);
		break;
	case 2:
		prop = of_get_property(pdev->dev.of_node, "pddrive_nf_s2", &size);
		break;
	case 3:
		prop = of_get_property(pdev->dev.of_node, "pddrive_nf_s3", &size);
		break;
	default:
		break;
	}

	if (prop) {
		if (size)
			counter = size / sizeof(u32);

		for (i=0; i<counter; i++) {
			emmc_port->pddrive_nf[i] = of_read_number(prop, 1 + i);
			printk(KERN_ERR "[%s] get pad driving : 0x%x\n",__func__, emmc_port->pddrive_nf[i]);
		}
	} else {
		pr_err("[%s] no driving nf warning !! \n",__func__);
	}

	prop = of_get_property(pdev->dev.of_node, "phase_tuning", &size);
	if (prop) {
		emmc_port->tx_tuning = of_read_number(prop, 1);
		emmc_port->rx_tuning = of_read_number(prop, 2);
		printk(KERN_ERR "[%s] get tx tuning switch : %u\n",__func__, emmc_port->tx_tuning);
		printk(KERN_ERR "[%s] get rx tuning switch : %u\n",__func__, emmc_port->rx_tuning);
	} else {
		emmc_port->tx_tuning = 1;	//if we do not get this node in device tree, we should tune phase by kernel
		emmc_port->rx_tuning = 1;
		printk(KERN_INFO "[%s] no phase_tuning switch node !! \n",__func__);
	}

	prop = of_get_property(pdev->dev.of_node, "dqs_tuning", &size);
	if (prop) {
		emmc_port->dqs_tuning = of_read_number(prop, 1);
		printk(KERN_ERR "[%s] get dqs tuning switch : %u\n",__func__, emmc_port->dqs_tuning);
	} else {
		emmc_port->dqs_tuning = 1;	//if we do not get this node, we should tune dqs value by kernel
		printk(KERN_INFO "[%s] no dqs_tuning switch node !! \n",__func__);
	}

	if(emmc_port->dqs_tuning==0) {
		emmc_port->dqs = of_read_number(prop, 2);
                printk(KERN_ERR "[%s] get dqs tuning reference value : %u\n",__func__, emmc_port->dqs);
		emmc_port->cmd_dly_tap = of_read_number(prop, 3);
		printk(KERN_ERR "[%s] get cmd_dly_tap tuning reference value : %u\n",__func__, emmc_port->cmd_dly_tap);
	}

	prop = of_get_property(pdev->dev.of_node, "reference_phase", &size);
	if (prop) {
		emmc_port->tx_user_defined = of_read_number(prop, 1);
		emmc_port->tx_reference_phase = of_read_number(prop, 2);
		emmc_port->rx_user_defined = of_read_number(prop, 3);
		emmc_port->rx_reference_phase = of_read_number(prop, 4);
		if(emmc_port->tx_user_defined) printk(KERN_ERR "[%s] get User defined tx reference phase: %u\n",__func__, emmc_port->tx_reference_phase);
		if(emmc_port->rx_user_defined) printk(KERN_ERR "[%s] get User defined rx reference phase: %u\n",__func__, emmc_port->rx_reference_phase);
	} else {
		emmc_port->tx_user_defined = 0;
		emmc_port->tx_reference_phase = 0x0;
		emmc_port->rx_user_defined = 0;
		emmc_port->rx_reference_phase = 0x0;
		printk(KERN_INFO "[%s] no tx & rx reference phase switch node !! \n",__func__);
	}

	prop = of_get_property(pdev->dev.of_node, "dqs_dly_tape", &size);
        if (prop) {
                emmc_port->dqs_dly_tape = of_read_number(prop, 1);
                printk(KERN_ERR "[%s] get dqs_dly_tape : %u\n",__func__, emmc_port->dqs_dly_tape);
        } else {
                emmc_port->dqs_dly_tape = 0x0;      //use 0x0 as default
                printk(KERN_INFO "[%s] no dqs_dly_tape switch node, use default 0x0 !! \n",__func__);
        }

	prop = of_get_property(pdev->dev.of_node, "emmc_tuning_addr", &size);
	if (prop) {
		emmc_port->emmc_tuning_addr = of_read_number(prop, 1);
		printk(KERN_ERR "[%s] GPT format: emmc tuning offset start from 0x%lx\n",__func__, emmc_port->emmc_tuning_addr);
	} else {
		prop = of_get_property(pdev->dev.of_node, "mbr_tuning_addr", &size);
		if (prop) {
			emmc_port->emmc_tuning_addr = of_read_number(prop, 1);      //if we do not get this node, we assume that the system uses MBR mode before Android O
			printk(KERN_ERR "[%s] MBR format: emmc tuning offset start from 0x%lx\n\n",__func__, emmc_port->emmc_tuning_addr);
		}
		else {
			emmc_port->emmc_tuning_addr = 0xa31000; //use default value in the factory partition end block addr - 1025 blocks
			printk(KERN_ERR "[%s] Use Default tuning addr: emmc tuning offset start from 0x%lx\n\n",__func__, emmc_port->emmc_tuning_addr);
		}
	}
#if defined(CONFIG_MMC_RTK_EMMC_PON)
	prop = of_get_property(pdev->dev.of_node, "pon_addr", &size);
	if (prop) {
		emmc_port->pon_blk_addr = of_read_number(prop, 1);
		printk(KERN_ERR "[%s] pon address starts from 0x%lx\n",__func__, emmc_port->pon_blk_addr);
	} else {
		emmc_port->pon_blk_addr = 0;      //if we do not get this node, we assume that the system uses MBR mode before Android O
		printk(KERN_INFO "[%s] No pon address node, emmc_port->pon_blk_addr = 0 !! \n",__func__);
	}
#endif
	prop = of_get_property(pdev->dev.of_node, "hs400_force_tuning", &size);
	if (prop) {
		emmc_port->hs400_force_tuning = of_read_number(prop, 1);
		printk(KERN_ERR "[%s] hs400 force tuning setting %u\n",__func__, emmc_port->hs400_force_tuning);
	} else {
		emmc_port->hs400_force_tuning = 0;      //if we do not get this node, we assume that the system uses MBR mode before Android O
		printk(KERN_INFO "[%s] No hs400_force_tuning node, emmc_port->hs400_force_tuning = 0 !\n",__func__);
	}

	prop = of_get_property(pdev->dev.of_node, "cmdq", &size);
	if (prop) {
		emmc_port->cmdq = of_read_number(prop, 1);
		printk(KERN_ERR "[%s] cmdq enable: %u\n", __func__, emmc_port->cmdq);
	}
	else {
		emmc_port->cmdq = 0;
		pr_err("[%s] no cmdq attribute, enable: %u\n",__func__, emmc_port->cmdq);
	}

	emmc_port->rstc_emmc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(emmc_port->rstc_emmc)) {
		printk(KERN_WARNING "%s: reset_control_get() returns %ld\n", __func__,
			PTR_ERR(emmc_port->rstc_emmc));
		emmc_port->rstc_emmc = NULL;
	}

	emmc_port->clk_en_emmc = devm_clk_get(&pdev->dev, "emmc");
	if (IS_ERR(emmc_port->clk_en_emmc)) {
		printk(KERN_WARNING "%s: clk_get() returns %ld\n", __func__,
			PTR_ERR(emmc_port->clk_en_emmc));
		emmc_port->clk_en_emmc = NULL;
	}
	emmc_port->clk_en_emmc_ip = devm_clk_get(&pdev->dev, "emmc_ip");
	if (IS_ERR(emmc_port->clk_en_emmc_ip)) {
		printk(KERN_WARNING "%s: clk_get() returns %ld\n", __func__,
			PTR_ERR(emmc_port->clk_en_emmc_ip));
		emmc_port->clk_en_emmc_ip = NULL;
	}
	clk_prepare_enable(emmc_port->clk_en_emmc);
	clk_prepare_enable(emmc_port->clk_en_emmc_ip);

	rtkemmc_writel(readl(emmc_port->crt_membase+0x454)|0x1, emmc_port->crt_membase+0x454);

	mmc->ocr_avail = MMC_VDD_30_31 
			| MMC_VDD_31_32
			| MMC_VDD_32_33
			| MMC_VDD_33_34
			| MMC_VDD_165_195;

	mmc->caps = MMC_CAP_4_BIT_DATA
		| MMC_CAP_8_BIT_DATA
		| MMC_CAP_SD_HIGHSPEED
		| MMC_CAP_MMC_HIGHSPEED
		| MMC_CAP_NONREMOVABLE
		| MMC_CAP_1_8V_DDR
		| MMC_CAP_UHS_DDR50
		| MMC_CAP_CMD23
		| MMC_CAP_ERASE;

	mmc->caps2 = (MMC_CAP2_NO_SDIO | MMC_CAP2_NO_SD);
	switch(emmc_port->speed_step)
	{
	case 0: //sdr50
		mmc->caps &= ~(MMC_CAP_UHS_DDR50|MMC_CAP_1_8V_DDR);
		mmc->caps2 &= ~(MMC_CAP2_HS200_1_8V_SDR);
		break;
	case 1: //ddr50
		mmc->caps2 &= ~(MMC_CAP2_HS200_1_8V_SDR);
		break;
	case 2: //hs200
		mmc->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
		break;
	case 3: //hs400
		mmc->caps2 |= (MMC_CAP2_HS200_1_8V_SDR|MMC_CAP2_HS400_1_8V);
		break;
	}

	if(emmc_port->cmdq==1)  {
		mmc->caps2 |= (MMC_CAP2_CQE|MMC_CAP2_CQE_DCMD);
                emmc_port->cq_host = cqhci_pltfm_init(pdev);
                if(PTR_ERR(emmc_port->cq_host)==-EINVAL ||
                        PTR_ERR(emmc_port->cq_host)==-ENOMEM ||
                        PTR_ERR(emmc_port->cq_host)==-EBUSY) {
                        pr_err("Unable to get the cmdq related attribute !!!\n");
                        emmc_port->cmdq = 0;
			mmc->caps2 &= ~(MMC_CAP2_CQE|MMC_CAP2_CQE_DCMD);
                }
                else {
                        emmc_port->cq_host->ops = &rtk_cqhci_host_ops;
                        cqhci_init(emmc_port->cq_host, mmc, 0);
                }
        }

	ret = rtkemmc_allocate_dma_buf(emmc_port);
	if(ret)
		goto out;

	mmc->caps2 |= (1 << 9);
	mmc->f_min = 300000;        //300K
	mmc->f_max = 400000000; //400M
	mmc->max_segs = 256;	//the max number of nodes in the scatterlist
	mmc->max_blk_size   = 512;
	mmc->max_blk_count  = 0x800;
	mmc->max_seg_size   = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_req_size   = mmc->max_blk_size * mmc->max_blk_count;

	spin_lock_init(&emmc_port->lock);
	init_rwsem(&emmc_port->cr_rw_sem);
	tasklet_init(&emmc_port->req_end_tasklet, rtkemmc_req_end_tasklet, (unsigned long)emmc_port);

	rtkemmc_hold_int_dec();       /* hold status interrupt */
	rtkemmc_clr_int_sta();
	ret = request_irq(emmc_port->irq, rtkemmc_irq, IRQF_SHARED, DRIVER_NAME, emmc_port);   //rtkemmc_interrupt
	if (ret) {
		pr_err("%s: cannot assign irq %d\n", DRIVER_NAME, emmc_port->irq);
		goto out;
	}

	timer_setup(&emmc_port->timer, rtkemmc_timeout_timer, 0);
	rtkemmc_set_pin_mux(emmc_port);

	rtkemmc_init(emmc_port);

	platform_set_drvdata(pdev, mmc);
	
	ret = mmc_add_host(mmc);
	if (ret)
		goto out;

	sync(emmc_port);
	g_bResuming=0;
	g_bTuning=0;

	printk(KERN_NOTICE "%s: %s driver initialized\n",
	mmc_hostname(mmc), DRIVER_NAME);

	rtkemmc_wait_block_dev_ready(mmc);

	return 0;

out:
	if (emmc_port) {
		if (emmc_port->irq)
			free_irq(emmc_port->irq, emmc_port);

		if (emmc_port->emmc_membase)
			iounmap(emmc_port->emmc_membase);
		if (emmc_port->crt_membase)
			iounmap(emmc_port->crt_membase);
	}

	if (mmc)
		mmc_free_host(mmc);

	rtkemmc_free_dma_buf(emmc_port);

	return ret;
}

static int __exit rtkemmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	MMCPRINTF("\n");

	device_remove_file(&pdev->dev, &dev_attr_emmc_info);
	device_remove_file(&pdev->dev, &dev_attr_tuning_info);
	device_remove_file(&pdev->dev, &dev_attr_gpp_info);
	device_remove_file(&pdev->dev, &dev_attr_protect_region_setting);

	if (mmc) {
		struct rtkemmc_host *emmc_port = mmc_priv(mmc);
		flush_scheduled_work();
		rtkemmc_free_dma_buf(emmc_port);
		mmc_remove_host(mmc);
		if(!mmc){
			printk("eMMC host have removed.\n");
        	}
		free_irq(emmc_port->irq, emmc_port);

		del_timer_sync(&emmc_port->timer);
		iounmap(emmc_port->emmc_membase);
		iounmap(emmc_port->crt_membase);
		iounmap(emmc_port->sb2_membase);
		iounmap(emmc_port->mux_mis_membase);
                iounmap(emmc_port->iso_blk_membase);
                iounmap(emmc_port->m2tmx_membase);
#if defined(CONFIG_MMC_RTK_EMMC_PON)
		devm_gpiod_put(pdev->dev, emmc_port->emmc_pon_gpio);
#endif
		nvmem_cell_put(emmc_port->cell);
		mmc_free_host(mmc);
	}
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id rtkemmc_ids[] = {
        { .compatible = "realtek,rtd13xx-emmc" },
        { /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, rtkemmc_ids);

/**************************************************/
/* driver / device attache area                   */
/**************************************************/
static struct platform_driver rtkemmc_driver = {
	.probe      = rtkemmc_probe,
	.remove     = __exit_p(rtkemmc_remove),
	.driver     =
	{
		.name   = "rtkemmc",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(rtkemmc_ids),
#ifdef CONFIG_PM
		.pm     = &rtk_dev_pm_ops
#endif
	},
	.shutdown   = rtkemmc_shutdown,
};
module_platform_driver(rtkemmc_driver);

MODULE_AUTHOR("Jim Tsai");
MODULE_DESCRIPTION("Realtek EMMC Host Controller driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtkemmc");
