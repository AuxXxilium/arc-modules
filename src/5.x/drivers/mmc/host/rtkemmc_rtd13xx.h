/*
 *  Copyright (C) 2010 Realtek Semiconductors, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RTKEMMC_H
#define __RTKEMMC_H

#include "reg_mmc_rtd13xx.h"
#include "mmc_debug.h"
#include <linux/nvmem-consumer.h>

#ifdef CONFIG_REALTEK_PCBMGR
#include <mach/pcbMgr.h>
#ifdef CONFIG_REALTEK_GPIO
#include <mach/venus_gpio.h>
#define EMMC_SHOUTDOWN_PROTECT
#endif
#endif

#define EMMC_MAX_SCRIPT_BLK   128

//debug
//#define MMC_DBG
#ifdef MMC_DBG
#define MMCPRINTF(fmt, args...)   printk(fmt,## args)
#else
#define MMCPRINTF(fmt, args...)
#endif

/* cmd1 sector mode */
#define MMC_SECTOR_ADDR		0x40000000

#define EMMC_DEFAULT_PHASE_VALUE        (0xff)
#define EMMC_PLL_HS200                  (0xa6)
#define EMMC_PLL_SDR50                  (0x57)
#define EMMC_CLK_DIV512                 (0x200)
#define EMMC_CLK_DIV4                   (0x4)
#define EMMC_CLK_DIV1                   (0x1)
#define CMDQ_DISABLED			(0x30f0001)
#define CMDQ_ENABLED			(0x30f0101)

#define EMMC_BLK_SZ                     (0x200)
#define EMMC_CMD21_BLK_SZ               (0x80)
#define EMMC_WR_PROT_BLK_SZ             (0x4)

#define RED_BOLD "\033[31;1m"
#define BLU_BOLD "\033[34;1m"
#define YEL_BOLD "\033[33;1m"
#define GRN_BOLD "\033[32;1m"
#define CYAN_BOLD_ITALIC "\033[36;1;3m"
#define RESET "\033[0;m"

struct backupRegs {
	u32                     sdmasa_r;       //0x98012000
	u16                     blocksize_r;    //0x98012004
	u16                     blockcount_r;   //0x98012006
	u16                     xfer_mode_r;    //0x9801200c
	u8                      host_ctrl1_r;   //0x98012028
	u8                      pwr_ctrl_r;     //0x98012029
	u8                      bgap_ctrl_r;    //0x9801202a
	u16                     clk_ctrl_r;     //0x9801202c
	u8                      tout_ctrl_r;    //0x9801202e

	u16                     normal_int_stat_en_r;   //0x98012034
	u16                     error_int_stat_en_r;    //0x98012036
	u16                     normal_int_signal_en_r; //0x98012038
	u16                     error_int_signal_en_r;  //0x9801203a
	u16                     auto_cmd_stat_r;        //0x9801203c
	u16                     host_ctrl2_r;           //0x9801203e
	u32                     adma_sa_low_r;          //0x98012058
	u8                      mshc_ctrl_r;            //0x98012208
	u8                      ctrl_r;                 //0x9801222c
	u32                     other1;                 //0x98012420
	u32                     dummy_sys;              //0x9801242c
	u32                     dqs_ctrl1;              //0x98012498
	u32                     wcmd_ctrl;              //0x98012554

	u32                     rdq_ctrl0;              //0x98012530
	u32                     rdq_ctrl1;              //0x98012534
	u32                     rdq_ctrl2;              //0x98012538
	u32                     rdq_ctrl3;              //0x9801253c
	u32                     rdq_ctrl4;              //0x98012540
	u32                     rdq_ctrl5;              //0x98012544
	u32                     rdq_ctrl6;              //0x98012548
	u32                     rdq_ctrl7;              //0x9801254c
	u32                     dq_ctrl_set;            //0x9801250c
	u32			ahb;
};

struct rtkemmc_host {
	struct mmc_host     *mmc;           /* MMC structure */
	u8                  cmd_opcode;

	struct mmc_request  *mrq;            /* Current request */

	volatile void __iomem	*emmc_membase;
	volatile void __iomem	*crt_membase;
	volatile void __iomem	*sb2_membase;	
	volatile void __iomem	*misc_membase;
	volatile void __iomem   *mux_mis_membase;
	volatile void __iomem   *iso_blk_membase;
	volatile void __iomem   *m2tmx_membase;
#if defined(CONFIG_MMC_RTK_EMMC_PON)
	volatile void __iomem   *norst_membase;
#endif
	u32                     pddrive_nf[5];
	struct nvmem_cell	*cell;
	spinlock_t		lock;
	struct tasklet_struct 	req_end_tasklet;
	struct rw_semaphore     cr_rw_sem;

	struct timer_list   	timer;
	struct completion   	*int_waiting;
	struct device       	*dev;
	int                 	irq;
	u8			time_setting;
#if 0
	int			irq_num;
#endif
	dma_addr_t          	dma_paddr;
	dma_addr_t          	desc_paddr;
	unsigned char*          dma_vaddr;
        unsigned int*           desc_vaddr;
	u32			tmout;
	u16			normal_interrupt; //2030
	u16			error_interrupt;  //2032
	u16			auto_error_interrupt;	//203c
	u8			rpmb_cmd; // it is rpmb cmd flag. When receiving CMD23, set to 1

	u32                     speed_step;
	u32			dqs_dly_tape;
	unsigned long		emmc_tuning_addr;

	u32                     tx_phase;
	u32                     rx_phase;
	u32                     dqs;
	u32                     cmd_dly_tap;

	struct reset_control*   rstc_emmc;
	struct clk*             clk_en_emmc;
	struct clk*             clk_en_emmc_ip;

	u8			suspend;
	struct backupRegs       backreg;

	u8                      hs400_force_tuning;
	u8		tx_tuning; //flag  that tx tuning need to be performed
	u8		rx_tuning; //flag  that rx tuning need to be performed
	u8		dqs_tuning;
	u8		tx_user_defined;
	u8		rx_user_defined;
	u8		tx_reference_phase;
	u8		rx_reference_phase;

	struct cqhci_host	*cq_host;
	u8			cmdq;
	u8			cmdq_reenable;
	u8			retune;
	u8			switch_partition;
#if defined(CONFIG_MMC_RTK_EMMC_PON)
	unsigned long pon_blk_addr;
	struct gpio_desc *emmc_pon_gpio;
	struct gpio_desc *emmc_pon_toggle_gpio;
#endif
};

struct sd_cmd_pkt {
	struct mmc_host     *mmc;       /* MMC structure */
	struct rtkemmc_host   *emmc_port;
	struct mmc_command  *cmd;    /* cmd->opcode; cmd->arg; cmd->resp; cmd->data */
	struct mmc_data     *data;
	unsigned char       *dma_buffer;
	u16                 byte_count;
	u16                 block_count;

	u32                 flags;
	u32			cmd_para;
	u8                  rsp_len;
	u32                 timeout;
};

#define MAX_CMD_RETRY_COUNT 4

#define RCA_SHIFTER             16

/* move from c file *** */
#define BYTE_CNT            0x200

#define RTK_FAIL            0x3  /* DMA error & cmd parser error */
#define RTK_RMOV            0x2  /* card removed */
#define RTK_TOUT            0x1  /* time out include DMA finish & cmd parser finish */
#define RTK_SUCC            0x0
#define CR_TRANSFER_FAIL    0x4

/* send status event */
#define STATE_IDLE          0
#define STATE_READY         1
#define STATE_IDENT         2
#define STATE_STBY          3
#define STATE_TRAN          4
#define STATE_DATA          5
#define STATE_RCV           6
#define STATE_PRG           7
#define STATE_DIS           8

#define rtkemmc_get_int_sta(normal_interrupt, error_interrupt, auto_error_interrupt)	\
	do {	\
		sync(emmc_port);	\
		*(u16 *)normal_interrupt = readw(emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_R);   \
		*(u16 *)error_interrupt = readw(emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_R);   \
		*(u16 *)auto_error_interrupt = readw(emmc_port->emmc_membase+EMMC_AUTO_CMD_STAT_R);	\
            } while (0)

//clear status register, we always keep the card interrupt, card insertion, removal status because the eMMC is unremovable
#define rtkemmc_clr_int_sta()                                                                              \
	do {                                                                                                \
		rtkemmc_writew(readw(emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_R)&0xffff, emmc_port->emmc_membase+EMMC_ERROR_INT_STAT_R); \
		rtkemmc_writew(readw(emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_R)&0xfeff, emmc_port->emmc_membase+EMMC_NORMAL_INT_STAT_R); \
	} while(0)

//mask all emmc interrupts
#define rtkemmc_hold_int_dec()    \
	do {      \
                writew(0x0,emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R); \
                writew(0x0,emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R); \
		sync(emmc_port);	\
	} while(0)

//for cmdq, we do not need cmd and xfer done, only cqe event
#define rtkemmc_en_cqe_int()  \
	do { \
		writew(0xfefc,emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R); \
		writew(EMMC_ALL_ERR_SIGNAL_EN,emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R); \
		sync(emmc_port);	\
	} while(0)

//used for data, r1b case, we mask cmd done interrupt
#define rtkemmc_en_xfer_int()  \
	do {  \
                writew(0xfefe,emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R); \
                writew(EMMC_ALL_ERR_SIGNAL_EN,emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R); \
		sync(emmc_port);	\
	} while(0)

//used for none-stream case (cmd w/wo/ resp)
#define rtkemmc_en_cd_int()  \
	do {    \
                writew(0xfefd,emmc_port->emmc_membase+EMMC_NORMAL_INT_SIGNAL_EN_R); \
                writew(EMMC_ALL_ERR_SIGNAL_EN,emmc_port->emmc_membase+EMMC_ERROR_INT_SIGNAL_EN_R); \
		sync(emmc_port);	\
	} while(0)	

#define rtkemmc_writel(val, addr) \
	do {    \
		sync(emmc_port);					\
		writel(val, addr);					\
	} while(0)

#define rtkemmc_writew(val, addr) \
	do {    \
		sync(emmc_port);                                        \
		writew(val, addr);					\
	} while(0)

#define rtkemmc_writeb(val, addr) \
	do {    \
		sync(emmc_port);                                        \
		writeb(val, addr);					\
	} while(0)

static const char *const state_tlb[11] = {
	"STATE_IDLE",
	"STATE_READY",
	"STATE_IDENT",
	"STATE_STBY",
	"STATE_TRAN",
	"STATE_DATA",
	"STATE_RCV",
	"STATE_PRG",
	"STATE_DIS",
	"STATE_BTST",
	"STATE_SLEEP"
};

/* remove from c file &&& */

/* rtk function definition */

/* rtk function definition */
int rtkemmc_send_cmd25(struct rtkemmc_host *emmc_port,int,unsigned long, int,int*, unsigned int);
int rtkemmc_send_cmd18(struct rtkemmc_host *emmc_port,int,unsigned long, unsigned int);

irqreturn_t cqhci_irq(struct mmc_host *mmc, u32 intmask, int cmd_error, int data_error);
int cqhci_init(struct cqhci_host *cq_host, struct mmc_host *mmc, bool dma64);
struct cqhci_host *cqhci_pltfm_init(struct platform_device *pdev);
void cqhci_dumpregs(struct cqhci_host *cq_host);
#endif
