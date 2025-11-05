#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  libata-scsi.c - helper library for ATA
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/driver-api/libata.rst
 *
 *  Hardware documentation available from
 *  - http://www.t10.org/
 *  - http://www.t13.org/
 */

#include <linux/compat.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <linux/libata.h>
#include <linux/hdreg.h>
#include <linux/uaccess.h>
#include <linux/suspend.h>
#include <asm/unaligned.h>
#include <linux/ioprio.h>
#include <linux/of.h>

#include "libata.h"
#include "libata-transport.h"

#ifdef MY_ABC_HERE
#include <linux/glob.h>
#endif  /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/synobios.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/synolib.h>
#include <linux/math64.h>
#include <linux/sort.h>
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE)
#include <linux/synosata.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/pci.h>
#include <linux/synolib.h>
extern int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size);
extern struct klist syno_ata_port_head;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/syno_gpio.h>
extern DISK_PWRCTRL_TYPE SYNO_GET_DISK_PWR_TYPE(int index);
extern int SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(int index);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/synobios.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern int SYNO_CTRL_HDD_POWERON(int index, int value);
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
#include <linux/random.h>
#include <linux/synolib.h>
#include <linux/synosata.h>

extern unsigned int guiWakeupDisksNum;
static int giGroupDisks = 0;
static int giWakingDisks = 0;
static unsigned long gulLastWake = 0;
DEFINE_SPINLOCK(SYNOLastWakeLock);
extern int giSynoSpinupGroup[SYNO_SPINUP_GROUP_MAX];
extern int giSynoSpinupGroupNum;
extern int giSynoSpinupGroupDelay;
static int gCurrentSpinupGroupNum = 0;
static int giNeedWakeAll = 0;
#endif /* MY_DEF_HERE */

#define ATA_SCSI_RBUF_SIZE	576

static DEFINE_SPINLOCK(ata_scsi_rbuf_lock);
static u8 ata_scsi_rbuf[ATA_SCSI_RBUF_SIZE];

typedef unsigned int (*ata_xlat_func_t)(struct ata_queued_cmd *qc);

static struct ata_device *__ata_scsi_find_dev(struct ata_port *ap,
					const struct scsi_device *scsidev);

#define RW_RECOVERY_MPAGE 0x1
#define RW_RECOVERY_MPAGE_LEN 12
#define CACHE_MPAGE 0x8
#define CACHE_MPAGE_LEN 20
#define CONTROL_MPAGE 0xa
#define CONTROL_MPAGE_LEN 12
#define ALL_MPAGES 0x3f
#define ALL_SUB_MPAGES 0xff


static const u8 def_rw_recovery_mpage[RW_RECOVERY_MPAGE_LEN] = {
	RW_RECOVERY_MPAGE,
	RW_RECOVERY_MPAGE_LEN - 2,
	(1 << 7),	/* AWRE */
	0,		/* read retry count */
	0, 0, 0, 0,
	0,		/* write retry count */
	0, 0, 0
};

static const u8 def_cache_mpage[CACHE_MPAGE_LEN] = {
	CACHE_MPAGE,
	CACHE_MPAGE_LEN - 2,
	0,		/* contains WCE, needs to be 0 for logic */
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	0,		/* contains DRA, needs to be 0 for logic */
	0, 0, 0, 0, 0, 0, 0
};

static const u8 def_control_mpage[CONTROL_MPAGE_LEN] = {
	CONTROL_MPAGE,
	CONTROL_MPAGE_LEN - 2,
	2,	/* DSENSE=0, GLTSD=1 */
	0,	/* [QAM+QERR may be 1, see 05-359r1] */
	0, 0, 0, 0, 0xff, 0xff,
	0, 30	/* extended self test time, see 05-359r1 */
};

static ssize_t ata_scsi_park_show(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap;
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long now;
	unsigned int msecs;
	int rc = 0;

	ap = ata_shost_to_port(sdev->host);

	spin_lock_irq(ap->lock);
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		rc = -ENODEV;
		goto unlock;
	}
	if (dev->flags & ATA_DFLAG_NO_UNLOAD) {
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	link = dev->link;
	now = jiffies;
	if (ap->pflags & ATA_PFLAG_EH_IN_PROGRESS &&
	    link->eh_context.unloaded_mask & (1 << dev->devno) &&
	    time_after(dev->unpark_deadline, now))
		msecs = jiffies_to_msecs(dev->unpark_deadline - now);
	else
		msecs = 0;

unlock:
	spin_unlock_irq(ap->lock);

	return rc ? rc : snprintf(buf, 20, "%u\n", msecs);
}

static ssize_t ata_scsi_park_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap;
	struct ata_device *dev;
	long int input;
	unsigned long flags;
	int rc;

	rc = kstrtol(buf, 10, &input);
	if (rc)
		return rc;
	if (input < -2)
		return -EINVAL;
	if (input > ATA_TMOUT_MAX_PARK) {
		rc = -EOVERFLOW;
		input = ATA_TMOUT_MAX_PARK;
	}

	ap = ata_shost_to_port(sdev->host);

	spin_lock_irqsave(ap->lock, flags);
	dev = ata_scsi_find_dev(ap, sdev);
	if (unlikely(!dev)) {
		rc = -ENODEV;
		goto unlock;
	}
	if (dev->class != ATA_DEV_ATA &&
	    dev->class != ATA_DEV_ZAC) {
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	if (input >= 0) {
		if (dev->flags & ATA_DFLAG_NO_UNLOAD) {
			rc = -EOPNOTSUPP;
			goto unlock;
		}

		dev->unpark_deadline = ata_deadline(jiffies, input);
		dev->link->eh_info.dev_action[dev->devno] |= ATA_EH_PARK;
		ata_port_schedule_eh(ap);
		complete(&ap->park_req_pending);
	} else {
		switch (input) {
		case -1:
			dev->flags &= ~ATA_DFLAG_NO_UNLOAD;
			break;
		case -2:
			dev->flags |= ATA_DFLAG_NO_UNLOAD;
			break;
		}
	}
unlock:
	spin_unlock_irqrestore(ap->lock, flags);

	return rc ? rc : len;
}
DEVICE_ATTR(unload_heads, S_IRUGO | S_IWUSR,
	    ata_scsi_park_show, ata_scsi_park_store);
EXPORT_SYMBOL_GPL(dev_attr_unload_heads);

#ifdef MY_ABC_HERE
int (*funcSYNOSATADiskLedCtrl) (int iHostNum, SYNO_DISK_LED diskLed) = NULL;
EXPORT_SYMBOL(funcSYNOSATADiskLedCtrl);

/* control the color of disk led  */
static ssize_t
syno_sata_disk_led_store(struct device *device,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	struct Scsi_Host *shost = class_to_shost(device);
	long led;
	int rc;

	if (NULL == funcSYNOSATADiskLedCtrl) {
		return -EINVAL;
	}
	rc = kstrtol(buf, 10, &led);
	if (rc) {
		return -EINVAL;
	}

	rc = funcSYNOSATADiskLedCtrl(shost->host_no, led);
	return len;
}
DEVICE_ATTR(syno_sata_disk_led_ctrl, S_IWUSR,
			NULL, syno_sata_disk_led_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_sata_disk_led_ctrl);
#endif /* MY_ABC_HERE */

void ata_scsi_set_sense(struct ata_device *dev, struct scsi_cmnd *cmd,
			u8 sk, u8 asc, u8 ascq)
{
	bool d_sense = (dev->flags & ATA_DFLAG_D_SENSE);

	if (!cmd)
		return;

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	scsi_build_sense_buffer(d_sense, cmd->sense_buffer, sk, asc, ascq);
}

void ata_scsi_set_sense_information(struct ata_device *dev,
				    struct scsi_cmnd *cmd,
				    const struct ata_taskfile *tf)
{
	u64 information;

	if (!cmd)
		return;

	information = ata_tf_read_block(tf, dev);
	if (information == U64_MAX)
		return;

	scsi_set_sense_information(cmd->sense_buffer,
				   SCSI_SENSE_BUFFERSIZE, information);
}

static void ata_scsi_set_invalid_field(struct ata_device *dev,
				       struct scsi_cmnd *cmd, u16 field, u8 bit)
{
	ata_scsi_set_sense(dev, cmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in CDB" */
	scsi_set_sense_field_pointer(cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE,
				     field, bit, 1);
}

static void ata_scsi_set_invalid_parameter(struct ata_device *dev,
					   struct scsi_cmnd *cmd, u16 field)
{
	/* "Invalid field in parameter list" */
	ata_scsi_set_sense(dev, cmd, ILLEGAL_REQUEST, 0x26, 0x0);
	scsi_set_sense_field_pointer(cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE,
				     field, 0xff, 0);
}


#if defined(MY_ABC_HERE) || defined (MY_ABC_HERE)
/**
 * Query this ap support pm control capacity
 *
 * @param ap [IN] ata port
 *
 * @return 0: not support
 *         1: support
 */
int iIsSynoPmCtlSupport(const struct ata_port *ap)
{
	int ret = 0;

	if (NULL == ap) {
		DBGMESG("ap is NULL, can't check pm control support\n");
		goto END;
	}

#ifdef MY_ABC_HERE
	if (-1 != ap->syno_internal_slot_index) {
		/* Internal disks case */
		if (SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(ap->syno_internal_slot_index + 1)) {
			ret = 1;
			goto END;
		}
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (ap->nr_pmp_links && syno_is_synology_pm(ap)) {
		ret = 1;
		goto END;
	}
#endif /* MY_ABC_HERE */

END:
	return ret;
}
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined (MY_ABC_HERE)
/**
 * Check if this port irqoff on
 *
 * @param [IN] ata port
 *
 * @return
 *  1: irqoff on
 *  0: not on
 *
 * */
int
iIsSynoIRQOff(const struct ata_port *ap)
{
	unsigned long flags = 0;
	int iRet = 0;

	if (NULL == ap) {
		goto END;
	}

	/* if no commands and pflags only on ATA_PFLAG_SYNO_IRQ_OFF and ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE,
	 * it means irq off */
	spin_lock_irqsave(ap->lock, flags);
	if (0 == ap->nr_active_links &&
		(ap->pflags == (ATA_PFLAG_SYNO_IRQ_OFF | ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE) ||
		 ap->pflags == (ATA_PFLAG_SYNO_IRQ_OFF | ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE | ATA_PFLAG_SYNO_DS_PWROFF) ||
		 ap->pflags == (ATA_PFLAG_SYNO_IRQ_OFF | ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE | ATA_PFLAG_EXTERNAL) ||
		 ap->pflags == (ATA_PFLAG_SYNO_IRQ_OFF | ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE | ATA_PFLAG_SYNO_DS_PWROFF | ATA_PFLAG_EXTERNAL))) {
		iRet = 1;
	}
	spin_unlock_irqrestore(ap->lock, flags);

END:
	return iRet;
}
EXPORT_SYMBOL(iIsSynoIRQOff);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
/**
 * Eliminate CPU usage in scemd. while there is no disks in the
 * PM. Libata may need this because the bug in sil24 that need
 * qc_defer.
 */
typedef struct _tag_SYNO_GPIO_TASK {
	/* delay work */
	struct delayed_work work;

	/* target ata port */
	struct ata_port *ap;

	/* gpio pkg */
	SYNO_PM_PKG pm_pkg;

	/* complete interface */
	struct completion wait;

	/* indicate result */
	unsigned char blIsErr;

	/* indicate read or write */
	unsigned char blIsRead;

	/* indicate retry or not */
	unsigned char blRetry;

} SYNO_GPIO_TASK;

static u8 inline
defer_gpio_cmd(struct ata_port *ap, u32 input, u8 rw)
{
	u8 ret = 0;

	/* we don't want to insert any gpio while the port is in error_handling */
	if (ap->pflags & (~ATA_PFLAG_EXTERNAL)) {
		ret = 1;
		goto END;
	}

END:
	return ret;
}

/**
 * using scsi command to enable/disable 9705 GPO
 *
 * @param blEnable    [IN] enable or disable
 * @param link        [IN] Should not be NULL
 * @param sdev        [IN] Should not be NULL
 *
 * @return 0: success
 *         Otherwise: fail
 *
 * Note: On 9705, GPI and GPO are the same pin, so each pin can
 *       only be treated as input or output at one time,
 *       Before reading, need to set "output_enable" to LOW (disable).
 *       Before writing, need to set "output_enable" to HIGH (enable).
 */

int
syno_pm_gpio_output_enable_with_sdev(bool blEnable,
									 struct ata_link *link,
									 struct scsi_device *sdev)
{
	int ret = 0;
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u16 feature = SATA_PMP_GSCR_9705_GPO_EN;
	u8 sense[SCSI_SENSE_BUFFERSIZE];
	struct scsi_sense_hdr sshdr;

	/* Only GPI1~GPI8(GPIO 0~4,11~13) need to set LOW. */
	u32 var = (blEnable ? 0xFFFFF : 0xFC7C0);

	if (!syno_pm_is_9705(sata_pmp_gscr_vendor(link->device->gscr),
			sata_pmp_gscr_devid(link->device->gscr))) {
		goto END;
	}

	if (NULL == link || NULL == sdev) {
		ret = 1;
		goto END;
	}

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	memset(sense, 0, sizeof(sense));

	scsi_cmd[0]  = ATA_16;
	scsi_cmd[1]  = (3 << 1) | 1;
	scsi_cmd[3]  = (feature >> 8) & 0xff;
	scsi_cmd[4]  = feature & 0xff;
	scsi_cmd[13] = link->pmp;

	scsi_cmd[6]  = var & 0xff;
	scsi_cmd[8]  = (var >> 8) & 0xff;
	scsi_cmd[10] = (var >> 16) & 0xff;
	scsi_cmd[12] = (var >> 24) & 0xff;
	scsi_cmd[14] = ATA_CMD_PMP_WRITE;

	ret = scsi_execute(sdev, scsi_cmd, DMA_NONE, NULL, 0, sense, &sshdr, (10*HZ), 5, 0, 0, NULL);

END:

	return ret;
}

/**
 * Using scsi command to issue jmb575 led command
 *
 * @param ap	[IN] Should not be NULL
 * @param sde	[IN] Should not be NULL
 * @param pPkg	[IN] Should not be NULL
 * @param rw	[IN] read or write
 *
 * @return 			0: success
 * 			Otherwise: fail
 */
int syno_jmb_575_led_ctl_with_scmd(struct ata_port *ap,
					struct scsi_device *sdev,
					u8 *pLedMask,
					u8 rw)
{
	struct scsi_sense_hdr sshdr;
	unsigned long flags = 0;
	int cmd_result;
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 sense[SCSI_SENSE_BUFFERSIZE];
	int ret = -EIO;
	int iRetries = 0;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	memset(sense, 0, sizeof(sense));

	if (NULL == ap || NULL == sdev || NULL == pLedMask) {
		goto END;
	}

	/* Get gpio ctrl lock in 2s */
	spin_lock_irqsave(ap->lock, flags);
	while ((ap->link.uiStsFlags & SYNO_STATUS_GPIO_CTRL) && (SYNO_PMP_GPIO_TRIES < iRetries)) {
		spin_unlock_irqrestore(ap->lock, flags);
		schedule_timeout_uninterruptible(HZ/2);
		spin_lock_irqsave(ap->lock, flags);
		++iRetries;
	}

	if (SYNO_PMP_GPIO_TRIES <= iRetries) {
		DBGMESG("syno_gpio_with_scmd get gpio lock timeout\n");
		spin_unlock_irqrestore(ap->lock, flags);
		goto RELEASE_LOCK;
	}
	/* lock to prevent others to do pmp gpio control */
	ap->link.uiStsFlags |= SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);

	scsi_cmd[0] = ATA_16;
	scsi_cmd[1]  = (3 << 1) | 1;

	/* Command */
	scsi_cmd[14] = ATA_CMD_PMP_SYNO_LED_GPIO;

	if (READ == rw) {
		scsi_cmd[2] = 0x20;
		/* LBA(0:7) */
		scsi_cmd[8] = 0x40; /* Read GPIO */
	} else {
		/* LBA(0:7) */
		scsi_cmd[8] = *pLedMask;
	}

	cmd_result = scsi_execute(sdev, scsi_cmd, DMA_NONE, NULL, 0,
				  sense, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {
		u8 *desc = sense + 8;

		if (WRITE == rw) {
			ret = 0;
			goto RELEASE_LOCK;
		}

		cmd_result &= ~(0xFF<<24);
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			struct scsi_sense_hdr sshdr;
			scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE,
					     &sshdr);
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		*pLedMask = desc[7];
	}

	if (cmd_result) {
		goto RELEASE_LOCK;
	}

	ret = 0;

RELEASE_LOCK:
	/* unlock to let others can do pmp gpio control */
	spin_lock_irqsave(ap->lock, flags);
	ap->link.uiStsFlags &= ~SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);
END:
	return ret;
}

static int syno_pm_jmb575_poll(struct ata_port *ap,
						struct scsi_device *sdev,
						char *buf,
						int sectors)
{
	/* Scsi command */
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 sense[SCSI_SENSE_BUFFERSIZE];
	struct scsi_sense_hdr sshdr;
	u8 *argbuf = NULL;
	int argsize = 0;

	/* Lock */
	int iRetries = 0;
	unsigned long flags = 0;

	int cmd_result;
	int ret = -EIO;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));	
	memset(sense, 0, sizeof(sense));

	if (NULL == ap || NULL == sdev || NULL == buf || 0 >= sectors) {
		printk("%s: Parameters check failed\n", __func__);
		goto END;
	}

	if(!IS_SYNOLOGY_RX1223RP(ap->PMSynoUnique)) {
		goto END;
	}

	/* Get Lock in 2s */
	spin_lock_irqsave(ap->lock, flags);
	while ((ap->link.uiStsFlags & SYNO_STATUS_GPIO_CTRL) && (SYNO_PMP_GPIO_TRIES < iRetries)) {
		spin_unlock_irqrestore(ap->lock, flags);
		schedule_timeout_uninterruptible(HZ/2);
		spin_lock_irqsave(ap->lock, flags);
		++iRetries;
	}

	/* Get Lock Timeout */
	if (SYNO_PMP_GPIO_TRIES <= iRetries) {
		printk("%s: Failed to get lock\n", __func__);
		spin_unlock_irqrestore(ap->lock, flags);
		goto RELEASE_LOCK;
	}
	/* lock to prevent others to do pmp gpio control */
	ap->link.uiStsFlags |= SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);

	/* Init CDB */

	scsi_cmd[0] = ATA_16;
	scsi_cmd[1] = (0x04 << 1) | 0x01;
	scsi_cmd[2] = 0x28;
	scsi_cmd[4] = 0xE9;
	scsi_cmd[6] = 0x01;
	scsi_cmd[14] = 0xF0;

	argsize = sectors * ATA_SECT_SIZE;
	argbuf = kmalloc(argsize, GFP_KERNEL);

	/* Scsi execute */

	cmd_result = scsi_execute(sdev, scsi_cmd, DMA_FROM_DEVICE, buf, argsize,
				  sense, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {/* sense data available */
		cmd_result &= ~(0xFF<<24); /* DRIVER_SENSE is not an error */

		/* If we set cc then ATA pass-through will cause a
		 * check condition even if no error. Filter that. */
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}
	}

	if (cmd_result) {
		ret = -EIO;
		goto END;
	}

	ret = 0;

RELEASE_LOCK:
	/* unlock to let others can do pmp gpio control */
	spin_lock_irqsave(ap->lock, flags);
	ap->link.uiStsFlags &= ~SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);

END:
	kfree(argbuf);
	return ret;
}

/**
 * Using scsi command to issue ebox i2c command
 *
 * @param ap	[IN] Should not be NULL
 * @param sde	[IN] Should not be NULL
 * @param pPkg	[IN] Should not be NULL
 * @param rw	[IN] read or write
 *
 * @return 			0: success
 * 			Otherwise: fail
 */
int syno_i2c_with_scmd(struct ata_port *ap,
					struct scsi_device *sdev,
					SYNO_PM_I2C_PKG *pPkg,
					u8 rw)
{
	struct scsi_sense_hdr sshdr;
	unsigned long flags = 0;
	int cmd_result;
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 sense[SCSI_SENSE_BUFFERSIZE];
	int ret = -EIO;
	int iRetries = 0;
	int i = 0;

	/* i2c pkg to scmd mapping */
	const int i2c_pkg_to_scmd[7] = {10, 12, 7, 9, 11, 6, 5};
	const int desc_to_i2c_pkg[7] = {7, 9, 11, 6, 8, 10, 5};

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	memset(sense, 0, sizeof(sense));

	if (NULL == ap || NULL == sdev || NULL == pPkg) {
		goto END;
	}

	/* Get gpio ctrl lock in 2s */
	spin_lock_irqsave(ap->lock, flags);
	while ((ap->link.uiStsFlags & SYNO_STATUS_GPIO_CTRL) && (SYNO_PMP_GPIO_TRIES < iRetries)) {
		spin_unlock_irqrestore(ap->lock, flags);
		schedule_timeout_uninterruptible(HZ/2);
		spin_lock_irqsave(ap->lock, flags);
		++iRetries;
	}

	if (SYNO_PMP_GPIO_TRIES <= iRetries) {
		DBGMESG("syno_gpio_with_scmd get gpio lock timeout\n");
		spin_unlock_irqrestore(ap->lock, flags);
		goto RELEASE_LOCK;
	}
	/* lock to prevent others to do pmp gpio control */
	ap->link.uiStsFlags |= SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);


	scsi_cmd[0] = ATA_16;
	scsi_cmd[1]  = (3 << 1) | 1;

	/* LBA(0:7) */
	scsi_cmd[8] = pPkg->offset;
	/* Command */
	scsi_cmd[14] = ATA_CMD_PMP_SYNO_I2C;

	if (READ == rw) {
		scsi_cmd[2] = 0x20;
		/* Feature(15:8) */
		scsi_cmd[3] = 1;
		/* Feature(0:7) */
		scsi_cmd[4] = pPkg->addr << 1 | 0x01; /* Read */
		/* Device */
		scsi_cmd[13] = pPkg->len;
	} else {
		/* Feature(0:7) */
		scsi_cmd[4] = pPkg->addr << 1;
		/* Device */
		scsi_cmd[13] = pPkg->len + 1; /* Add 1 for i2c dev offset */
		/* Write Data */
		for (i = 0; i < pPkg->len; i++) {
			if (ARRAY_SIZE(i2c_pkg_to_scmd) > i) {
				scsi_cmd[i2c_pkg_to_scmd[i]] = pPkg->inputData[i];
			}
		}
	}

	cmd_result = scsi_execute(sdev, scsi_cmd, DMA_NONE, NULL, 0,
				  sense, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {
		u8 *desc = sense + 8;

		if (WRITE == rw) {
			ret = 0;
			goto END;
		}

		cmd_result &= ~(0xFF<<24);
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			struct scsi_sense_hdr sshdr;
			scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE,
					     &sshdr);
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		for (i = 0; i < pPkg->len; i++) {
			if (ARRAY_SIZE(desc_to_i2c_pkg) > i) {
				pPkg->resultData[i] = desc[desc_to_i2c_pkg[i]];
			}
		}
	}

	if (cmd_result) {
		goto END;
	}

	ret = 0;

RELEASE_LOCK:
	/* unlock to let others can do pmp gpio control */
	spin_lock_irqsave(ap->lock, flags);
	ap->link.uiStsFlags &= ~SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);
END:
	return ret;
}

/**
 * using scsi command to issue gpio command
 *
 * @param ap     [IN] Should not be NULL
 * @param sdev   [IN] Should not be NULL
 * @param pPkg   [IN] Should not be NULL
 * @param rw     [IN] read or write
 *
 * @return 0: success
 *         Otherwise: fail
 */
int syno_gpio_with_scmd(struct ata_port *ap,
					struct scsi_device *sdev,
					SYNO_PM_PKG *pPkg,
					u8 rw)
{
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 sense[SCSI_SENSE_BUFFERSIZE];
	int ret = -EIO;
	int cmd_result;
	struct scsi_sense_hdr sshdr;
	unsigned long flags = 0;
	int iRetries = 0;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	memset(sense, 0, sizeof(sense));

	if (NULL == ap) {
		goto END;
	}

	/* Get gpio ctrl lock in 2s */
	spin_lock_irqsave(ap->lock, flags);
	while ((ap->link.uiStsFlags & SYNO_STATUS_GPIO_CTRL) && (SYNO_PMP_GPIO_TRIES < iRetries)) {
		spin_unlock_irqrestore(ap->lock, flags);
		schedule_timeout_uninterruptible(HZ/2);
		spin_lock_irqsave(ap->lock, flags);
		++iRetries;
	}

	if (SYNO_PMP_GPIO_TRIES <= iRetries) {
		DBGMESG("syno_gpio_with_scmd get gpio lock timeout\n");
		spin_unlock_irqrestore(ap->lock, flags);
		goto END;
	}
	/* lock to prevent others to do pmp gpio control */
	ap->link.uiStsFlags |= SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);

	syno_pm_device_info_set(ap, rw, pPkg);

	if (READ == rw) {
		if (syno_pm_gpio_output_enable_with_sdev(false, &ap->link, sdev)) {
			goto END;
		}
	} else if (WRITE == rw) {
		if (syno_pm_gpio_output_enable_with_sdev(true, &ap->link, sdev)) {
			goto END;
		}
	}

	if (READ == rw) {
		scsi_cmd[2] = 0x20;
		scsi_cmd[14] = ATA_CMD_PMP_READ;
	} else {
		if (pPkg->encode) {
			pPkg->encode(pPkg, WRITE);
		}
		scsi_cmd[6] = pPkg->var & 0xff;
		scsi_cmd[8] = (pPkg->var >> 8) & 0xff;
		scsi_cmd[10] = (pPkg->var >> 16) & 0xff;
		scsi_cmd[12] = (pPkg->var >> 24) & 0xff;
		scsi_cmd[14] = ATA_CMD_PMP_WRITE;
	}

	scsi_cmd[0] = ATA_16;
	scsi_cmd[1]  = (3 << 1) | 1;
	scsi_cmd[3] = (pPkg->gpio_addr >> 8) & 0xff;
	scsi_cmd[4] = pPkg->gpio_addr & 0xff;
	scsi_cmd[13] = ap->link.pmp;

	cmd_result = scsi_execute(sdev, scsi_cmd, DMA_NONE, NULL, 0,
				  sense, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {
		u8 *desc = sense + 8;

		if (WRITE == rw) {
			goto END;
		}

		cmd_result &= ~(0xFF<<24);
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			struct scsi_sense_hdr sshdr;
			scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE,
					     &sshdr);
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		pPkg->var = desc[5] | desc[7] << 8 | desc[9] << 16 | desc[11] << 24;
		pPkg->decode(pPkg, READ);
	}

	if (cmd_result) {
		goto END;
	}

	/*
	 * A strange situation appears on DX1211/RX1211 that the write command is sent but the device does not act.
	 * Delaying for several microseconds can solve such an issue, however, the actual root cause is not confirmed.
	 * This might be just a walkaround.
	 */
	if (WRITE == rw) {
		msleep(50);
	}

	ret = 0;
END:

	/* unlock to let others can do pmp gpio control */
	spin_lock_irqsave(ap->lock, flags);
	ap->link.uiStsFlags &= ~SYNO_STATUS_GPIO_CTRL;
	spin_unlock_irqrestore(ap->lock, flags);

	return ret;
}

static void
syno_gpio_task(struct work_struct *pWork)
{
	SYNO_GPIO_TASK *pTask = container_of(pWork, SYNO_GPIO_TASK, work.work);
	unsigned int (*gpio_func)(struct ata_link *, SYNO_PM_PKG *);
	unsigned int ret = 0;

	if (pTask->blIsRead) {
		gpio_func = syno_sata_pmp_read_gpio_core;
	} else {
		gpio_func = syno_sata_pmp_write_gpio_core;
	}

	pTask->blRetry = pTask->blIsErr = 0;

	ret = gpio_func(&(pTask->ap->link), &(pTask->pm_pkg));

	if (AC_ERR_OTHER == ret) {
		pTask->blRetry = 1;
	}

	if (0 != ret) {
		pTask->blIsErr = 1;
	}

	complete(&pTask->wait);
}

static void inline
syno_gpio_task_init(SYNO_GPIO_TASK *pTask,
					u8 rw,
					struct ata_port *ap)
{
	memset(pTask, 0, sizeof(*pTask));
	INIT_DELAYED_WORK(&(pTask->work), syno_gpio_task);
	init_completion(&(pTask->wait));
	pTask->blIsRead = (WRITE == rw)? 0 : 1;
	pTask->ap = ap;
}

static ssize_t
syno_gpio_read_with_sdev(struct ata_port *ap, char *buf, struct scsi_device *sdev)
{
	SYNO_PM_PKG pm_pkg;
	ssize_t len = -EIO;

	if (syno_gpio_with_scmd(ap, sdev, &pm_pkg, READ)) {
		sprintf(buf, "%s=\"\"%s", EBOX_GPIO_KEY, "\n");
	} else {
		len = sprintf(buf, "%s=\"0x%x\"%s", EBOX_GPIO_KEY, pm_pkg.var, "\n");
	}

	return len;
}


/**
 * issue ata command with scsi command, we append it at first
 * pm drive.
 *
 * @param ap     [IN] ata port. Should not be NULL
 * @param sdev   [IN] scsi device. Should not be NULL
 * @param input  [IN] the value we want to write into gpio
 *
 * @return 0: success
 * otherwise: fail
 */
static u8
syno_gpio_write_with_sdev(struct ata_port *ap, struct scsi_device *sdev, u32 input)
{
	SYNO_PM_PKG pm_pkg;

	pm_pkg.var = input;
	return syno_gpio_with_scmd(ap, sdev, &pm_pkg, WRITE);
}

struct ata_port* SynoEunitEnumPort(struct ata_port *pAp_master, struct klist_node *pAtaNode)
{
	struct ata_port *pAp = NULL;
	struct klist_node *ata_node = NULL;
	int slotNumber = -1;
	struct klist_iter klist_iter;
	memset(&klist_iter, 0, sizeof(klist_iter));

	if (NULL == pAp_master) {
		goto END;
	}

	if (-1 == (slotNumber = syno_external_libata_index_get(pAp_master))) {
		printk(KERN_DEBUG "Failed to get slotNumber for ata_port %d\n", pAp_master->print_id);
		goto END;
	}

	klist_iter_init_node(&syno_ata_port_head, &klist_iter, pAtaNode);
	for (ata_node = klist_next(&klist_iter); NULL != ata_node; ata_node = klist_next(&klist_iter)) {
		pAp = container_of(ata_node, struct ata_port, ata_port_list);

		if (syno_is_synology_pm(pAp) && slotNumber == syno_external_libata_index_get(pAp)) {
			break;	
		}

		pAp = NULL;
	}
	klist_iter_exit(&klist_iter);

END:
	return pAp;

}

/* find eunit master */
struct ata_port *SynoEunitFindMaster(struct ata_port *pAp)
{
	struct ata_port *pAp_master = NULL;
	struct klist_iter klist_iter;
	int slotNumber = -1;
	struct klist_node *ata_node = NULL;

	memset(&klist_iter, 0, sizeof(klist_iter));

	if (NULL == pAp) {
		goto END;
	}

	/* if this port is master, we return itself immediately */
	if (0 == pAp->PMSynoEMID) {
		pAp_master = pAp;
		goto END;
	}

	slotNumber = syno_external_libata_index_get(pAp);
	if (-1 == slotNumber) {
		printk(KERN_DEBUG "Failed to get slotNumber for ata_port %d\n", pAp->print_id);
		goto END;
	}

	klist_iter_init(&syno_ata_port_head, &klist_iter);
	for (ata_node = klist_next(&klist_iter); NULL != ata_node; ata_node = klist_next(&klist_iter)) {
		pAp_master = container_of(ata_node, struct ata_port, ata_port_list);
		if (0 == pAp_master->PMSynoEMID && slotNumber == syno_external_libata_index_get(pAp_master)) {
			break;
		}
		pAp_master = NULL;
	}
	klist_iter_exit(&klist_iter);

END:
	return pAp_master;
}

#ifdef MY_ABC_HERE
static void SynoEunitBindLock(struct ata_port *pAp_master, bool blset)
{
	struct klist_iter klist_iter;
	struct ata_port *ap = NULL;

	memset(&klist_iter, 0, sizeof(klist_iter));

	if (NULL == pAp_master) {
		goto END;
	}

	if (!syno_is_synology_pm(pAp_master)) {
		goto END;
	}

	while (NULL != (ap = SynoEunitEnumPort(pAp_master, ap? &ap->ata_port_list: NULL))) {
		/* This special lock is used to power on eunit in deep sleep state. */
		if (!ap->scsi_host->eunit_lock_configured) {
			ap->scsi_host->peunit_poweron_lock = &(pAp_master->scsi_host->eunit_poweron_lock);
			ap->scsi_host->puiata_eh_flag = &(pAp_master->scsi_host->uiata_eh_flag);
			ap->scsi_host->eunit_lock_configured = 1;
		}
		if (blset) {
			ap->scsi_host->is_eunit_deepsleep = 1;
		}
	}

END:
	return ;
}

/* check if any port in this EUnit still have errors or activeties
 *
 * @param host [IN] EUnit master port
 *
 * @return 0: not have errors or activeties
 *         1: have errors or activeties
 *        -1: failed
 */
static int SynoIsEunitPortActing(const struct ata_port *pAp_master)
{
	int iRet = -1;
	unsigned long flags = 0;
	struct klist_iter klist_iter;
	struct klist_node *ata_node = NULL;
	struct ata_port *ap = NULL;
	int slotNumber = -1;

	memset(&klist_iter, 0, sizeof(klist_iter));

	if (NULL == pAp_master) {
		goto END;
	}

	if (!syno_is_synology_pm(pAp_master)) {
		goto END;
	}

	slotNumber = syno_external_libata_index_get(pAp_master);
	if (-1 == slotNumber) {
		printk(KERN_DEBUG "Failed to get slotNumber for ata_port %d\n", pAp_master->print_id);
		goto END;
	}

	klist_iter_init(&syno_ata_port_head, &klist_iter);
	for (ata_node = klist_next(&klist_iter); NULL != ata_node; ata_node = klist_next(&klist_iter)) {
		ap = container_of(ata_node, struct ata_port, ata_port_list);
		if (slotNumber != syno_external_libata_index_get(ap)) {
			goto CONTINUE_FOR;
		}

		spin_lock_irqsave(ap->lock, flags);
		/* still have activities */
		if (ap->nr_active_links) {
			DBGMESG("%s EUnit %d have activities\n",
						(ap->PMSynoEMID == 0 ? "Master" : "Slave"),
						ap->print_id);
			iRet = 1;
		}
		/* have errors */
		if (ap->pflags & (~ATA_PFLAG_EXTERNAL)) {
			DBGMESG("%s EUnit %d have errors pflags 0x%x\n",
						(ap->PMSynoEMID == 0 ? "Master" : "Slave"),
						ap->print_id,
						ap->pflags);
			iRet = 1;
		}
		spin_unlock_irqrestore(ap->lock, flags);

		if (1 == iRet) {
			goto END;
		}

CONTINUE_FOR:
		ap = NULL;
	}
	klist_iter_exit(&klist_iter);

END:
	return iRet;
}
#endif /* MY_ABC_HERE */

/*
 * blWithLink is false: No matter any disk on ap, just raise/pull flag
 * blWithLink is true: If any disk(link) on ata port, then raise/pull the flag
 */
void SynoEunitFlagSet(struct ata_port *pAp_master, bool blset, unsigned int flag, bool blWithLink)
{
	struct klist_iter klist_iter;
	struct klist_node *ata_node = NULL;
	struct ata_port *ap = NULL;
	struct ata_link *link = NULL;
	unsigned long flags_ap;
	bool blAnyLink = false;
	int slotNumber = -1;

	memset(&klist_iter, 0, sizeof(klist_iter));

	if (NULL == pAp_master) {
		goto END;
	}

	if (!syno_is_synology_pm(pAp_master)) {
		goto END;
	}

	slotNumber = syno_external_libata_index_get(pAp_master);
	if (-1 == slotNumber) {
		printk(KERN_DEBUG "Failed to get slotNumber for ata_port %d\n", pAp_master->print_id);
		goto END;
	}

	klist_iter_init(&syno_ata_port_head, &klist_iter);
	for (ata_node = klist_next(&klist_iter); NULL != ata_node; ata_node = klist_next(&klist_iter)) {
		ap = container_of(ata_node, struct ata_port, ata_port_list);
		if (slotNumber != syno_external_libata_index_get(ap)) {
			goto CONTINUE_FOR;
		}

		if (blWithLink) {
			/* Check any link on ap */
			ata_for_each_link(link, ap, EDGE) {
				if (0 != link->sata_spd) {
					blAnyLink = true;
					break;
				}
			}
			/* If no link on ap, do not raise/pull flag. */
			if (!blAnyLink) {
				goto CONTINUE_FOR;
			}
		}

		spin_lock_irqsave(ap->lock, flags_ap);
		if (blset) {
			ap->pflags |= flag;
		} else {
			ap->pflags &= ~flag;
		}
		spin_unlock_irqrestore(ap->lock, flags_ap);

CONTINUE_FOR:
		ap = NULL;
	}
	klist_iter_exit(&klist_iter);

END:
	return ;
}

int syno_eunit_poweroff(struct ata_port *ap)
{
	struct ata_port *pAp_master = NULL;
	int iRet = -1;

	if (SYSTEM_POWER_OFF != system_state) {
		iRet = 0;
		goto END;
	}

	/* Only handle EUnit */
	if(!ap->nr_pmp_links) {
		DBGMESG("port %d is internal disk skip it\n", ap->print_id);
		iRet = 0;
		goto END;
	}

	/* this host already irqoff by other hosts, we needn't poweroff it again */
	if (iIsSynoIRQOff(ap)) {
		DBGMESG("port %d already irqoff, skip this poweroff control\n", ap->print_id);
		iRet = 0;
		goto END;
	}

	/* Because SynoEunitFlagSet, syno_libata_pm_power_ctl need find master too,
	 * so we use master here, that can save more time */
	pAp_master = SynoEunitFindMaster(ap);
	if (NULL == pAp_master) {
		printk("Can't find syno Eunit master to do power control\n");
		goto END;
	}
	
	DBGMESG("disk %d do poweroff task\n", ap->print_id);

	/* set ATA_PFLAG_SYNO_IRQ_OFF and ATA_PFLAG_SYNO_DS_PWROFF flag */
	SynoEunitFlagSet(pAp_master, 1, ATA_PFLAG_SYNO_IRQ_OFF, 0);
	SynoEunitFlagSet(pAp_master, 1, ATA_PFLAG_SYNO_DS_PWROFF, 0);

	/* poweroff */
	if (0 != syno_libata_pm_power_ctl(pAp_master, 0, 0)) {
		/* Not support PM port or GPIO cmd timeout (ex. DX510), it will go to this case.
		 * So this may not be an error, just by HW design */
		DBGMESG("disk %d poweroff task fail, it may still power on after DS poweroff\n", ap->print_id);
	}

	/* set ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE flag */
	SynoEunitFlagSet(pAp_master, 1, ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE, 0);
	iRet = 0;

END:
	return iRet;
}
EXPORT_SYMBOL(syno_eunit_poweroff);

/*
 *
 * change interface from syno_pm_gpio_show(struct device *dev, char *buf)
 * to syno_pm_gpio_show(struct device *dev, struct device_attribute *attr, char *buf)
 * to fit the DEVICE_ATTR macro defined in 2.6.32
*/
static ssize_t
syno_pm_gpio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct scsi_device *sdev = NULL;
	struct ata_device *pAtaDev = (struct ata_device *)ap->link.device;
	ssize_t len = -EIO;

	if (ap->nr_pmp_links &&
		syno_is_synology_pm(ap)) {
		if (defer_gpio_cmd(ap, 0, READ)) {
			sprintf(buf, "%s%s%s", EBOX_GPIO_KEY, "=\"\"", "\n");
			return len;
		} else if (NULL != (sdev = pAtaDev->sdev)) {
			return syno_gpio_read_with_sdev(ap, buf, sdev);
		} else {
			printk("can't find pm scsi device for gpio show\n");
		}
	} else {
		len = sprintf(buf, "%s%s%s", EBOX_GPIO_KEY, "=\"\"", "\n");
	}

	return len;
}

static ssize_t
syno_pm_gpio_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ata_device *pAtaDev = (struct ata_device *)ap->link.device;
	struct scsi_device *sdev = NULL;
	/* please man 2 write */
	ssize_t ret = -EIO;
	u32 input;

	sscanf(buf, "%x", &input);

	if (ap->nr_pmp_links &&
		syno_is_synology_pm(ap) &&
		!defer_gpio_cmd(ap, input, WRITE)) {
		u8 result = 0;

		if (NULL != (sdev = pAtaDev->sdev)) {
			result = syno_gpio_write_with_sdev(ap, sdev, input);
		} else {
			printk("can't find pm scsi device for store\n");
		}

		ret = !result ? count : -EIO;
	}
	return ret;
}
DEVICE_ATTR(syno_pm_gpio, S_IRUGO | S_IWUSR, syno_pm_gpio_show, syno_pm_gpio_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_pm_gpio);

/*
 *  show if power disabled while new expansion unit plugged in
 */
static ssize_t
syno_pm_gpio_power_disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t len = -EIO;

	len = sprintf(buf, "%d\n", ap->PMSynoPowerDisable);

	return len;
}

/*
 *  store power disable flag while new expansion unit plugged in
 */
static ssize_t
syno_pm_gpio_power_disable_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	//please man 2 write
	ssize_t ret = -EIO;
	u32 input;

	sscanf(buf, "%d", &input);

	if (1 == input) {
		ap->PMSynoPowerDisable = 1;
	} else {
		ap->PMSynoPowerDisable = 0;
	}

	return ret;
}
DEVICE_ATTR(syno_manutil_power_disable, S_IRUGO | S_IWUSR, syno_pm_gpio_power_disable_show, syno_pm_gpio_power_disable_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_manutil_power_disable);

static ssize_t
syno_pm_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t len = 0;
	//int index, start_idx;
	int NumOfPMPorts = 0;
	char szPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};

	if (ap->nr_pmp_links &&
		syno_is_synology_pm(ap)) {
		char szTmp[BDEVNAME_SIZE];
		char *szTmp1 = NULL;
		szTmp1 = (char*) kcalloc(PAGE_SIZE, sizeof(char), GFP_KERNEL);
		if (NULL == szTmp1) {
			printk(KERN_WARNING "%s kmalloc failed\n", __FUNCTION__);
			len = 0;
			goto END;
		}

		NumOfPMPorts = syno_support_disk_num(sata_pmp_gscr_vendor(ap->link.device->gscr),
											 sata_pmp_gscr_devid(ap->link.device->gscr),
											 ap->PMSynoUnique);

		memset(szTmp, 0, sizeof(szTmp));

		/* syno_device_list, it will be empty */
		snprintf(buf, PAGE_SIZE, "%s%s%s%s", EBOX_INFO_DEV_LIST_KEY, "=\"", szTmp1, "\"\n");

		/* vendor id and device id */
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%s0x%x%s", EBOX_INFO_VENDOR_KEY, "\"",
				 sata_pmp_gscr_vendor(ap->link.device->gscr),
				 "\"\n");
		snprintf(szTmp1, PAGE_SIZE, "%s", szTmp);
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%s%x%s", EBOX_INFO_DEVICE_KEY, "\"",
				 sata_pmp_gscr_devid(ap->link.device->gscr),
				 "\"\n");
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

		/* error handle processing */
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%s%s%s", EBOX_INFO_ERROR_HANDLE, "\"",
				 (ap->pflags & (~ATA_PFLAG_EXTERNAL)) ? "yes" : "no",
				 "\"\n");
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%sv%d%s", EBOX_INFO_CPLDVER_KEY, "\"",
				 ap->PMSynoCpldVer,
				 "\"\n");

		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

#ifdef MY_ABC_HERE
		/* deepsleep support */
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%s%s%s", EBOX_INFO_DEEP_SLEEP, "\"",
				 iIsSynoDeepSleepSupport(ap) ? "yes" : "no",
				 "\"\n");
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);
#endif /* MY_ABC_HERE */

		/* irq off state */
		snprintf(szTmp,
				 BDEVNAME_SIZE,
				 "%s=%s%s%s", EBOX_INFO_IRQ_OFF, "\"",
				 iIsSynoIRQOff(ap) ? "yes" : "no",
				 "\"\n");
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

		/* unique model name and EMID*/
		if (IS_SYNOLOGY_RX413(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_RX413,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_RX1214(ap->PMSynoUnique)) {
			if (ap->PMSynoIsRP) {
				snprintf(szTmp,
						BDEVNAME_SIZE,
						"%s=\"%s\"\n%s=\"%d\"\n",
						EBOX_INFO_UNIQUE_KEY,
						EBOX_INFO_UNIQUE_RX1214RP,
						EBOX_INFO_EMID_KEY,
						ap->PMSynoEMID);
			} else {
				snprintf(szTmp,
						BDEVNAME_SIZE,
						"%s=\"%s\"\n%s=\"%d\"\n",
						EBOX_INFO_UNIQUE_KEY,
						EBOX_INFO_UNIQUE_RX1214,
						EBOX_INFO_EMID_KEY,
						ap->PMSynoEMID);
			}
		} else if(IS_SYNOLOGY_RX1217(ap->PMSynoUnique)) {
			if(ap->PMSynoIsRP) {
				snprintf(szTmp,
						BDEVNAME_SIZE,
						"%s=\"%s\"\n%s=\"%d\"\n",
						EBOX_INFO_UNIQUE_KEY,
						EBOX_INFO_UNIQUE_RX1217RP,
						EBOX_INFO_EMID_KEY,
						ap->PMSynoEMID);
			} else {
				snprintf(szTmp,
						BDEVNAME_SIZE,
						"%s=\"%s\"\n%s=\"%d\"\n",
						EBOX_INFO_UNIQUE_KEY,
						EBOX_INFO_UNIQUE_RX1217,
						EBOX_INFO_EMID_KEY,
						ap->PMSynoEMID);
			}
		} else if(IS_SYNOLOGY_RX415(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_RX415,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_DX1215(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_DX1215,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if(IS_SYNOLOGY_DX517(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_DX517,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_RX418(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_RX418,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_DX1222(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_DX1222,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_DX1215II(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_DX1215II,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else if (IS_SYNOLOGY_RX1223RP(ap->PMSynoUnique)) {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"%s\"\n%s=\"%d\"\n",
					EBOX_INFO_UNIQUE_KEY,
					EBOX_INFO_UNIQUE_RX1223RP,
					EBOX_INFO_EMID_KEY,
					ap->PMSynoEMID);
		} else {
			snprintf(szTmp,
					BDEVNAME_SIZE,
					"%s=\"Unknown\"\n%s=\"0\"\n", EBOX_INFO_UNIQUE_KEY, EBOX_INFO_EMID_KEY);
		}
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

		/* Host Unique ID */
		snprintf(szTmp,
				BDEVNAME_SIZE,
				"%s=\"%lx\"\n",
				EBOX_INFO_SATAHOST_KEY,
				(unsigned long)ap->host);

		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

		/* ATA Port Number */
		snprintf(szTmp,
				BDEVNAME_SIZE,
				"%s=\"%u\"\n",
				EBOX_INFO_PORTNO_KEY,
				ap->port_no);

		strncat(szTmp1, szTmp, BDEVNAME_SIZE);

		/* Pcie Path */
		if (ap->dev->bus && !strcmp("pci", ap->dev->bus->name)) {
			syno_pciepath_dts_pattern_get(to_pci_dev(ap->dev), szPciePath, sizeof(szPciePath));
			snprintf(szTmp,
				BDEVNAME_SIZE,
				"%s=\"%s\"\n",
				EBOX_INFO_PCIEPATH_KEY,
				szPciePath);
			strncat(szTmp1, szTmp, BDEVNAME_SIZE);
		}

		/* put it together */
		len = snprintf(buf, PAGE_SIZE, "%s%s", buf, szTmp1);
		kfree(szTmp1);
	} else {
		len = snprintf(buf, PAGE_SIZE, "%s%s%s", EBOX_INFO_DEV_LIST_KEY, "=\"\"", "\n");
	}

END:
	return len;
}

DEVICE_ATTR(syno_pm_info, S_IRUGO, syno_pm_info_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_pm_info);

typedef struct {
	unsigned char fanTach[4][2];
} SYNO_JMB575_POLL_DATA;

static ssize_t
syno_pm_i2c_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	int written = 0, i = 0;
	int fanIdx = 0;

	if (SYNO_PM_I2C_SYSFS_I2C_READ == ap->i2cOp && !ap->i2cPkg.blIsErr) {
		written += sprintf(buf, "%s=\"", EBOX_I2C_KEY);

		for(i = 0; i < ap->i2cPkg.len; i++)  {
			written += snprintf(buf+written, 20, "0x%02x%s",
								ap->i2cPkg.resultData[i],
								((ap->i2cPkg.len -1)  == i) ? "\"\n" : " ");
		}
	} else if (SYNO_PM_I2C_SYSFS_POLLING == ap->i2cOp) {
		if (NULL != ap->link.device->sdev && 0 == syno_pm_jmb575_poll(ap, ap->link.device->sdev, ap->sector_buf, 1)) {
			written += snprintf(buf+written, 25, "[%s]\n", EBOX_I2C_KEY);
			for (fanIdx = 0; fanIdx < ARRAY_SIZE(((SYNO_JMB575_POLL_DATA*)(ap->sector_buf))->fanTach); fanIdx++) {

				written += snprintf(buf+written, 25, "%s%d=\"0x%02x 0x%02x\"\n", 
						EBOX_I2C_POLLING_FAN,
						fanIdx + 1,
						((SYNO_JMB575_POLL_DATA*)(ap->sector_buf))->fanTach[fanIdx][0],
						((SYNO_JMB575_POLL_DATA*)(ap->sector_buf))->fanTach[fanIdx][1]);
			}

		}
	}
	return written;
}

static ssize_t
syno_pm_i2c_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t ret = -EIO;

	int i = 0;
	int offset = 0;
	int total_offset = 0;

	int ledIdx = 0;
	int setLedMask = 0;
	struct scsi_device *sdev = NULL;

	if (defer_gpio_cmd(ap, 0, READ) || NULL == (sdev = ap->link.device->sdev)) {
		ap->i2cPkg.blIsErr = true;
		DBGMESG("ata%d: skip ebox i2c control\n", ap->print_id);
		goto END;
	}

	if (0 == strncmp(buf, EBOX_I2C_SYSFS_OP_WRITE, strlen(EBOX_I2C_SYSFS_OP_WRITE))) {
		total_offset = strlen(EBOX_I2C_SYSFS_OP_WRITE);
		ap->i2cOp = SYNO_PM_I2C_SYSFS_I2C_WRITE;

		if (3 > sscanf(buf + total_offset, "%hx %hx %hu%n", &ap->i2cPkg.addr, &ap->i2cPkg.offset, &ap->i2cPkg.len, &offset)) {
			printk("I2C store parameter error\n");
			goto END;
		}

		total_offset += offset;
		ap->i2cPkg.len = ap->i2cPkg.len <= SYNO_PMP_I2C_MAX_DATA_LEN? ap->i2cPkg.len : SYNO_PMP_I2C_MAX_DATA_LEN;

		for (i = 0; i < ap->i2cPkg.len; i++) {
			if(0 >= sscanf(buf + total_offset, "%4hhx%n", &ap->i2cPkg.inputData[i], &offset)) {
				printk("I2C store parameter error\n");
				goto END;
			}
			total_offset += offset;
		}

		syno_i2c_with_scmd(ap, sdev, &ap->i2cPkg, WRITE);
	} else if (0 == strncmp(buf, EBOX_I2C_SYSFS_OP_READ, strlen(EBOX_I2C_SYSFS_OP_READ))) {
		total_offset = strlen(EBOX_I2C_SYSFS_OP_READ);
		ap->i2cOp = SYNO_PM_I2C_SYSFS_I2C_READ;

		if (3 > sscanf(buf + total_offset, "%hx %hx %hu%n", &ap->i2cPkg.addr, &ap->i2cPkg.offset, &ap->i2cPkg.len, &offset)) {
			printk("I2C store parameter error\n");
			goto END;
		}

		total_offset += offset;
		ap->i2cPkg.len = ap->i2cPkg.len <= SYNO_PMP_I2C_MAX_DATA_LEN? ap->i2cPkg.len : SYNO_PMP_I2C_MAX_DATA_LEN;

		ap->i2cPkg.blIsErr = false;
		syno_i2c_with_scmd(ap, sdev, &ap->i2cPkg, READ);

	} else if (0 == strncmp(buf, EBOX_I2C_SYSFS_OP_JMB575_LED_CTL, strlen(EBOX_I2C_SYSFS_OP_JMB575_LED_CTL))) {
		total_offset = strlen(EBOX_I2C_SYSFS_OP_JMB575_LED_CTL);

		if (2 > sscanf(buf + total_offset, "%d %d", &ledIdx, &setLedMask)) {
			ret = count;
			goto END;
		}

		if (syno_sata_jmb575_disk_led_set_with_scmnd(&ap->link, ledIdx, setLedMask)) {
			printk("JMB575 Led Ctrl Fail\n");
		}
	} else if (0 == strncmp(buf, EBOX_I2C_SYSFS_OP_POLL, strlen(EBOX_I2C_SYSFS_OP_POLL))) {
		ap->i2cOp = SYNO_PM_I2C_SYSFS_POLLING;
	}

	ret = count;
END:

	return ret;
}
DEVICE_ATTR(syno_pm_i2c, S_IRUGO | S_IWUSR, syno_pm_i2c_show, syno_pm_i2c_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_pm_i2c);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE

/**
 * Query this ap deep sleep capacity
 *
 * @param ap [IN] ata port
 *
 * @return 0: not support
 *         1: support
 */
int iIsSynoDeepSleepSupport(struct ata_port *ap)
{
	int iRet = 0;

	if (NULL == ap) {
		DBGMESG("ap is NULL, can't check deep sleep support\n");
		goto END;
	}

	if (-1 != ap->syno_internal_slot_index) {
		/* Internal case */
		if (PWRCTRL_TYPE_GPIO != SYNO_GET_DISK_PWR_TYPE(ap->syno_internal_slot_index+1)) {
			goto END;
		}
	}

	/* this ap can't support power control, so it must can't support deep sleep */
	iRet = iIsSynoPmCtlSupport(ap);

END:
	return iRet;
}

/* this function will set the ata power
 * TODO: You can't do spin_lock(ap) before you call this function
 * @param host [IN]  the scsi host
 *        blPowerOn  1: set poweron 0: set poweroff
 *
 * @return 0: success
 *        -1: failed
 */
static int syno_libata_port_power_ctl(struct ata_port *ap, u8 pwrOp)
{
	unsigned long flags = 0;
	int iIsDSPowerOff = 0;
	int iRet = -1;

	u8 blPowerOn = 0;
	if(NULL == ap) {
		goto END;
	}

	if(0 == iIsSynoPmCtlSupport(ap)) {
		DBGMESG("disk %d doesn't support pm control\n", ap->print_id);
		goto END;
	}

	blPowerOn = (pwrOp & (SYNO_PWR_OP_POWER_ON | SYNO_PWR_OP_WAKE))? 1 : 0;

	// Check if still have command
	spin_lock_irqsave(ap->lock, flags);
	iIsDSPowerOff = ap->pflags & ATA_PFLAG_SYNO_DS_PWROFF;
	spin_unlock_irqrestore(ap->lock, flags);
	if(!blPowerOn && ap->nr_active_links && !iIsDSPowerOff) {
		printk("WARNING: disk %d still have command, poweroff it may cause data inconsistency\n",
				ap->print_id);
		WARN_ON(1);
	}
	DBGMESG("disk %d do pm control pwrOp %d\n", ap->print_id, pwrOp);

#if defined(MY_ABC_HERE)
	if(-1 != ap->syno_internal_slot_index && SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(ap->syno_internal_slot_index + 1)) {
		/* Internal disks case: only support deep sleep ap, we can do pm control */
		SYNO_CTRL_HDD_POWERON(ap->syno_internal_slot_index + 1, blPowerOn);

		if(blPowerOn) {
			mdelay(3000);
		}

		iRet = 0;
		goto END;
	}
#endif /* MY_ABC_HERE && MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (syno_is_synology_pm(ap)) {
		/* this port is a master , we no need to get master */
		struct ata_port *pAp_master = NULL;

		pAp_master = SynoEunitFindMaster(ap);
		if (NULL == pAp_master) {
			printk("Can't find syno Eunit master to do power control\n");
		}

		if (0 != syno_libata_pm_power_ctl(pAp_master, pwrOp, 0)) {
			goto END;
		}

		iRet = 0;
		goto END;
	}
#endif /* MY_ABC_HERE */

	iRet = 0;
END:
	return iRet;
}

/**
 * set/clear ata port pflags
 *
 * @parami ap [IN/OUT]: the ata port
 *         ulFlag [IN]: the pflag which need to be set/clear
 *         blSet [IN]: set or clear
 *
 * @retrun
 *  0: success others: fail
 */
int SynoFlagSet(struct ata_port *ap, const unsigned int ulFlag, const u8 blSet)
{
	int iRet = -1;
	unsigned long flags;
#ifdef MY_ABC_HERE
	struct ata_port *pAp_slave = NULL;
	struct ata_port *pAp_master = NULL;
	int iIsActing = 0;
#endif /* MY_ABC_HERE */

	if (NULL == ap) {
		goto END;
	}

	/* Internal port flag set */
	if (!ap->nr_pmp_links) {
		spin_lock_irqsave(ap->lock, flags);
		if(blSet) {
			ap->pflags |= ulFlag;
		} else {
			ap->pflags &= ~(ulFlag);
		}
		spin_unlock_irqrestore(ap->lock, flags);
	}
#ifdef MY_ABC_HERE
	else { /* Eunit flag set */
		if(0 == ap->PMSynoEMID) {
			pAp_master = ap;
		} else {
			pAp_master = SynoEunitFindMaster(ap);
			if (NULL == pAp_master) {
				printk("Can't find syno Eunit master to set flag\n");
				goto END;
			}
		}
		/* set master flag */
		spin_lock_irqsave(pAp_master->lock, flags);
		if(ATA_PFLAG_SYNO_IRQ_OFF == ulFlag && blSet && pAp_master->nr_active_links) {
			DBGMESG("WARNING:Master disk %d still have command\n",
					pAp_master->print_id);
			iIsActing = 1;
		}
		if(blSet) {
			pAp_master->pflags |= ulFlag;
		} else {
			pAp_master->pflags &= ~(ulFlag);
		}
		spin_unlock_irqrestore(pAp_master->lock, flags);

		/* find slave disks */
		while (NULL != (pAp_slave = SynoEunitEnumPort(pAp_master, pAp_slave? &pAp_slave->ata_port_list: NULL))) {
			spin_lock_irqsave(pAp_slave->lock, flags);
			if(ATA_PFLAG_SYNO_IRQ_OFF == ulFlag && blSet && pAp_slave->nr_active_links) {
				DBGMESG("Slave disk %d still have command\n",
						pAp_slave->print_id);
				iIsActing = 1;
			}
			if(blSet) {
				pAp_slave->pflags |= ulFlag;
				DBGMESG("Set pflags 0x%x for ata_port %d\n", ulFlag, pAp_slave->print_id);
			} else {
				pAp_slave->pflags &= ~(ulFlag);
				DBGMESG("Unset pflags 0x%x for ata_port %d\n", ulFlag, pAp_slave->print_id);
			}
			spin_unlock_irqrestore(pAp_slave->lock, flags);
		}
		if (iIsActing) {
			goto END;
		}
	}
#endif /* MY_ABC_HERE */

	iRet = 0;

END:
	return iRet;
}

static struct scsi_device *
look_up_scsi_dev_from_ap(struct ata_port *ap)
{
	struct scsi_device *sdev = NULL;
	struct ata_link *link = NULL;
	struct ata_device *dev = NULL;

	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ALL) {
			if (dev->sdev && SDEV_RUNNING == dev->sdev->sdev_state) {
				sdev = dev->sdev;
				return sdev;
			}
		}
	}
	return NULL;
}

/**
 * NOTE: shouldn't call spin_lock(ap) before you call this function
 * deep sleep control
 *
 * @param host [IN] scsi host
 * @param blSet [IN] 0: unset this host deep sleep
 *                       set   this host to deep sleep
 *
 * @return 0: success
 *         others: fail
 */
int
syno_libata_set_deep_sleep(struct ata_port *ap, const u8 blSet)
{
	struct ata_port *pAp_master = NULL;
	int iRet = -1;
	unsigned long flags;
	u8 blPowerOn = !blSet;

	if (NULL == look_up_scsi_dev_from_ap(ap)&& blSet) {
		/* Only skip when entering deep sleep, chips may trigger error with no disks in deepsleep, need to wakeup. */
		DBGMESG("port %d have no disks, ignore it\n", ap->print_id);
		iRet = 0;
		goto END;
	}
	if(0 == iIsSynoDeepSleepSupport(ap)) {
		DBGMESG("disk %d doesn't support deep sleep\n", ap->print_id);
		iRet = 0;
		goto END;
	}
	if (blSet && 1 == iIsSynoIRQOff(ap)) {
		DBGMESG("disk %d already irqoff, skip this irq off control\n", ap->print_id);
		iRet = 0;
		goto END;
	}

	/* set/unset ATA_PFLAG_SYNO_IRQ_OFF */
	if (blSet) {
		if(ap->nr_active_links) {
			DBGMESG("disk %d still have command, can't set deep sleep\n",ap->print_id);
			goto END;
		}
	}

	pAp_master = ap;

#ifdef MY_ABC_HERE
	if(ap->nr_pmp_links) {
		pAp_master = SynoEunitFindMaster(ap);
		if (NULL == pAp_master) {
			printk("Can't find syno Eunit master to do power control\n");
			goto END;
		}
		if (blSet && 1 == SynoIsEunitPortActing(pAp_master)) {
			DBGMESG("EUnit %d is still acting, can't set deepsleep\n", pAp_master->print_id);
			goto END;
		}
	}
#endif /* MY_ABC_HERE */

	/* check if master locked, if locked we must wait here */
	spin_lock_irqsave(pAp_master->lock, flags);
	if (pAp_master->iIsDeepCtlLock) {
		DBGMESG("disk %d master %d deep locked, wait here\n", ap->print_id, pAp_master->print_id);
		while(pAp_master->iIsDeepCtlLock) {
			spin_unlock_irqrestore(pAp_master->lock, flags);
			schedule_timeout_uninterruptible(HZ);
			spin_lock_irqsave(pAp_master->lock, flags);
		}
	}
	pAp_master->iIsDeepCtlLock = 1;
	spin_unlock_irqrestore(pAp_master->lock, flags);

	DBGMESG("disk %d do deep sleep control blSet %d\n", ap->print_id, blSet);
	/* set/unset ATA_PFLAG_SYNO_IRQ_OFF flag */
	if (SynoFlagSet(pAp_master, ATA_PFLAG_SYNO_IRQ_OFF, blSet)) {
		printk("Enit %d set ATA_PFLAG_SYNO_IRQ_OFF fail, reset now\n", pAp_master->print_id);
		spin_lock_irqsave(ap->lock, flags);
		ata_port_schedule_eh(ap);
		spin_unlock_irqrestore(ap->lock, flags);
		goto END;
	}

#ifdef MY_ABC_HERE
	if (!blSet) {
		struct ata_link *link;
		struct ata_device *dev;
		ata_for_each_link(link, pAp_master, EDGE) {
			ata_for_each_dev(dev, link, ALL) {
				if (dev->flags & ATA_DFLAG_NO_WCACHE) {
					DBGMESG("port %d set wcache disable action\n", pAp_master->print_id);
					dev->link->eh_info.dev_action[dev->devno] |= ATA_EH_WCACHE_DISABLE;
				}
			}
		}
	}
#endif /* MY_ABC_HERE */

	/* set pwr */
	if (0 != syno_libata_port_power_ctl(pAp_master, blPowerOn? SYNO_PWR_OP_WAKE : SYNO_PWR_OP_DEEPSLEEP)) {
		printk("ata port %d set deep sleep pwr fail blPowerOn %d\n", ap->print_id, blPowerOn);
		goto END;
	}

	/* if pm control success, set/unset ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE flag */
	if (SynoFlagSet(pAp_master, ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE, blSet)) {
		printk("Enit %d set ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE fail, reset now\n", pAp_master->print_id);
		spin_lock_irqsave(ap->lock, flags);
		ata_port_schedule_eh(ap);
		spin_unlock_irqrestore(ap->lock, flags);
		goto END;
	}

#ifdef MY_ABC_HERE
	/* Bind deepsleep lock and flags to master host on same eunit, for future wake up use. */
	if (ap->nr_pmp_links) {
		SynoEunitBindLock(pAp_master, blSet);
	}
#endif /* MY_ABC_HERE */

	iRet = 0;

END:
	if (pAp_master) {
		spin_lock_irqsave(pAp_master->lock, flags);
		if (pAp_master->iIsDeepCtlLock) {
			pAp_master->iIsDeepCtlLock = 0;
		}
		spin_unlock_irqrestore(pAp_master->lock, flags);
	}
	/* Set SYNO_STATUS_DEEP_SLEEP_FAILED pAp_master flag to prevent waking flag is set inproperly when wake up next time. */
	if (NULL != pAp_master && 0 != iRet && blSet) {
		spin_lock_irqsave(pAp_master->lock, flags);
		pAp_master->uiStsFlags |= SYNO_STATUS_DEEP_SLEEP_FAILED;
		spin_unlock_irqrestore(pAp_master->lock, flags);
	}

	return iRet;
}

static ssize_t
syno_power_ctrl_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t ret = -EIO;
	int iPwrOn = 0;

	sscanf(buf, "%d", &iPwrOn);
	syno_libata_port_power_ctl(ap, iPwrOn);

	ret = count;

	return ret;
}
DEVICE_ATTR(syno_power_ctrl, S_IWUSR, NULL, syno_power_ctrl_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_power_ctrl);

static ssize_t
syno_deep_sleep_ctrl_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	ssize_t ret = -EIO;
	int iBlSet = 0;

	if(0 == iIsSynoDeepSleepSupport(ap)) {
		goto END;
	}

	sscanf(buf, "%d", &iBlSet);
	if(syno_libata_set_deep_sleep(ap, iBlSet)) {
		goto END;
	}

	ret = count;

END:
	return ret;
}
DEVICE_ATTR(syno_deep_sleep_ctrl, S_IWUSR, NULL, syno_deep_sleep_ctrl_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_deep_sleep_ctrl);

/* query this device support deep sleep or not */
static ssize_t
syno_deep_sleep_support_show(struct device *device,
						     struct device_attribute *attr, char *buf)
{
	/* copy from ata_scsi_park_show to get ata_device */
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = NULL;
	ssize_t len = 0;
	int iSupport = 0;

	ap = ata_shost_to_port(sdev->host);
	iSupport = iIsSynoDeepSleepSupport(ap);
	/* +2, '0' and '\n' */
	len = snprintf(buf, 1 + 2, "%d%s", iSupport, "\n");

	return len;
}
DEVICE_ATTR(syno_deep_sleep_support, S_IRUGO, syno_deep_sleep_support_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_deep_sleep_support);

/* query this scsi host support pm control or not */
static ssize_t
syno_pm_control_support_show(struct device *dev,
						     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t len = 0;
	int iSupport = 0;

	iSupport = iIsSynoPmCtlSupport(ap);
	/* +2, '0' and '\n' */
	len = snprintf(buf, 1 + 2, "%d%s", iSupport, "\n");

	return len;
}
DEVICE_ATTR(syno_pm_control_support, S_IRUGO, syno_pm_control_support_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_pm_control_support);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#ifdef MY_ABC_HERE
static ssize_t
syno_seq_stat_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	struct ata_link *link;
	ssize_t len				= 0;
	char szTmp[512]			= {'\0'};
	int i = 0;

	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		goto END;
	}
	link = dev->link;

	// Disk uuid
	snprintf(szTmp, sizeof(szTmp), "%pU\n",
					link->latency_stat.uuid);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);
	// total IO cmd count.
	for (i = 0; i < SYNO_SEQ_SAMPLE_LBA_ZONE; i++) {
		snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu\n",
						link->seq_stat.u64TotalSampleBytes[i],
						link->seq_stat.u64TotalSampleTime[i],
						link->seq_stat.u64TotalSampleSkipBytes[i]);
		len += strlen(szTmp);
		strncat(buf, szTmp, PAGE_SIZE - len - 1);
	}
END:
	return len;
}
DEVICE_ATTR(syno_disk_seq_stat, S_IRUGO, syno_seq_stat_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_disk_seq_stat);
#endif /* MY_ABC_HERE */

static void disk_latency_hist_get(u64 u64TimeBuckets[SYNO_LATENCY_BUCKETS_END][32],
								char *szBuf, int cbBuf)
{
	ssize_t len				= 0;
	unsigned int j			= 0;
	unsigned int i			= 0;
	char szTmp[32]			= {'\0'};
	for (j = 0; j < SYNO_LATENCY_BUCKETS_END; j++) {
		for (i = 0; i < 32; i++) {
			snprintf(szTmp, sizeof(szTmp), "%llu ", u64TimeBuckets[j][i]);
			len += strlen(szTmp);
			strncat(szBuf, szTmp, cbBuf - len - 1);
		}
		szBuf[len - 1] = '\n';
	}
	return;
}

static ssize_t
syno_latency_read_hist_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	struct ata_link *link;
	ssize_t len = 0;

	// We not lock it to sacrifice some accuracy but decrease overhead.
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		goto END;
	}
	link = dev->link;

	disk_latency_hist_get(link->ata_latency.u64TimeBuckets[1], buf, PAGE_SIZE);
	len = strlen(buf);
	disk_latency_hist_get(link->ata_latency.u64RespTimeBuckets[1], buf + len, PAGE_SIZE - len - 1);
	len = strlen(buf);

END:
	return len;
}
DEVICE_ATTR(syno_disk_latency_read_hist, S_IRUGO, syno_latency_read_hist_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_disk_latency_read_hist);

static ssize_t
syno_latency_write_hist_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	struct ata_link *link;
	ssize_t len = 0;

	// We not lock it to sacrifice some accuracy but decrease overhead.
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		goto END;
	}
	link = dev->link;

	disk_latency_hist_get(link->ata_latency.u64TimeBuckets[2], buf, PAGE_SIZE);
	len = strlen(buf);
	disk_latency_hist_get(link->ata_latency.u64RespTimeBuckets[2], buf + len, PAGE_SIZE - len - 1);
	len = strlen(buf);

END:
	return len;
}
DEVICE_ATTR(syno_disk_latency_write_hist, S_IRUGO, syno_latency_write_hist_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_disk_latency_write_hist);

static ssize_t
syno_latency_other_hist_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	struct ata_link *link;
	ssize_t len = 0;

	// We not lock it to sacrifice some accuracy but decrease overhead.
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		goto END;
	}
	link = dev->link;

	disk_latency_hist_get(link->ata_latency.u64TimeBuckets[0], buf, PAGE_SIZE);
	len = strlen(buf);
	disk_latency_hist_get(link->ata_latency.u64RespTimeBuckets[0], buf + len, PAGE_SIZE - len - 1);
	len = strlen(buf);

END:
	return len;
}
DEVICE_ATTR(syno_disk_latency_other_hist, S_IRUGO, syno_latency_other_hist_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_disk_latency_other_hist);

static ssize_t
syno_latency_stat_show(struct device *device,
		   struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	struct ata_link *link;
	ssize_t len = 0;
	unsigned long ulFlags = 0;
	char szTmp[512] = {'\0'};
	u64 u64CurrentTime = 0;

	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		goto END;
	}
	link = dev->link;

	// Disk uuid
	snprintf(szTmp, sizeof(szTmp), "%pU\n",
					link->latency_stat.uuid);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);
	// total IO cmd count.
	snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu\n",
					link->latency_stat.u64TotalCount[0],
					link->latency_stat.u64TotalCount[1],
					link->latency_stat.u64TotalCount[2]);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);

	// total IO cmd process time.
	snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu\n",
					link->latency_stat.u64TotalTime[0],
					link->latency_stat.u64TotalTime[1],
					link->latency_stat.u64TotalTime[2]);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);

	// total IO cmd reponse time.
	snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu\n",
					link->latency_stat.u64TotalRespTime[0],
					link->latency_stat.u64TotalRespTime[1],
					link->latency_stat.u64TotalRespTime[2]);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);

	// total IO bytes.
	snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu\n",
					link->latency_stat.u64TotalBytes[0],
					link->latency_stat.u64TotalBytes[1],
					link->latency_stat.u64TotalBytes[2]);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);

	spin_lock_irqsave(ap->lock, ulFlags);
	u64CurrentTime = cpu_clock(0);
	// Batch status.
	snprintf(szTmp, sizeof(szTmp), "%llu %llu %llu %llu %llu\n",
					link->latency_stat.u64TotalBatchCount,
					link->latency_stat.u64TotalBatchTime,
					link->ata_latency.u64BatchIssue,
					link->ata_latency.u64BatchComplete,
					u64CurrentTime);
	spin_unlock_irqrestore(ap->lock, ulFlags);
	len += strlen(szTmp);
	strncat(buf, szTmp, PAGE_SIZE - len - 1);

END:
	return len;
}
DEVICE_ATTR(syno_disk_latency_stat, S_IRUGO, syno_latency_stat_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_syno_disk_latency_stat);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static ssize_t
syno_port_thaw_store(struct device *dev, struct device_attribute *attr, const char * buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t ret = -EIO;
	int iThaw = 1;

	if(!ap) {
		goto END;
	}

	sscanf(buf, "%d", &iThaw);
	if (iThaw) {
		ata_port_schedule_eh(ap);
	} else {
		ata_port_printk(ap, KERN_ERR, "port freeze from sysfs control\n");
		ata_eh_freeze_port(ap);

#ifdef MY_ABC_HERE
		schedule_work(&(ap->SendPortDisEventTask));
#endif /* MY_ABC_HERE */
	}

	ret = count;

END:
	return ret;
}

static ssize_t
syno_port_thaw_show(struct device *dev, struct device_attribute *attr, char * buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	ssize_t len = -EIO;

	if(!ap) {
		goto END;
	}


	if (ap->pflags & ATA_PFLAG_FROZEN) {
		len = sprintf(buf, "%d%s", 0, "\n");
	} else {
		len = sprintf(buf, "%d%s", 1, "\n");
	}

END:
	return len;
}
DEVICE_ATTR(syno_port_thaw, S_IRUGO | S_IWUSR, syno_port_thaw_show, syno_port_thaw_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_port_thaw);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/**
 * show this dev power reset count
 **/
static ssize_t
syno_pwr_reset_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *ata_dev = ata_scsi_find_dev(ap, sdev);

	ssize_t len = -EIO;

	if (!ap || !ata_dev) {
        goto END;
    }

	len = sprintf(buf, "%d%s", ata_dev->iResetPwrCount, "\n");

END:
	return len;
}

/**
 * set this dev power reset count
 **/
static ssize_t
syno_pwr_reset_count_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
    struct ata_port *ap = ata_shost_to_port(sdev->host);
    struct ata_device *ata_dev = ata_scsi_find_dev(ap, sdev);

	int iSet = 0;
	ssize_t ret = -EIO;

    if (!ap || !ata_dev) {
        goto END;
    }

	sscanf(buf, "%d", &iSet);
	ata_dev->iResetPwrCount = iSet;

	ret = count;

END:
	return ret;
}
DEVICE_ATTR(syno_pwr_reset_count, S_IRUGO | S_IWUSR, syno_pwr_reset_count_show, syno_pwr_reset_count_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_pwr_reset_count);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/**
 * send an error event to user space
 **/
static ssize_t
syno_sata_error_event_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *ata_dev = ata_scsi_find_dev(ap, sdev);
	ssize_t ret = -EIO;
	int iStartIdx = 0;
	int iErrorCount = 0;
	int iDiskPortType = 0;
#ifdef MY_ABC_HERE
	DISK_PORT_TYPE diskPortType = UNKNOWN_DEVICE;
	int iDiskPortIndex = -1;
#endif /* MY_ABC_HERE */

	if (!ap || !ata_dev) {
		goto END;
	}
	sscanf(buf, "%d", &iErrorCount);

	iStartIdx = syno_libata_numeric_diskname_number_get(ata_dev->link);

#ifdef MY_ABC_HERE
	get_disk_port_type_and_index_by_ata_port(ap, &diskPortType, &iDiskPortIndex);
	iDiskPortType = (int) diskPortType;
#endif /* MY_ABC_HERE */

	if (func_synobios_event_handler) {
		func_synobios_event_handler(SYNO_EVENT_SATA_ERROR_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									SERR_10B_8B_ERR,
									ATA_ICRC,
									iDiskPortType);
		printk(KERN_ERR "----------------------- sent event: {SError: 10B8B} {Error: ICRC} --------------------\n");

		func_synobios_event_handler(SYNO_EVENT_SATA_ERROR_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									SERR_HANDSHAKE | SERR_DISPARITY,
									ATA_UNC,
									iDiskPortType);
		printk(KERN_ERR "----------------------- sent event: {SError: Dispar Handshk} {Error: UNC} --------------------\n");

		func_synobios_event_handler(SYNO_EVENT_SATA_ERROR_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									0,
									ATA_IDNF | ATA_ABORTED | ATA_UNC,
									iDiskPortType);
		printk(KERN_ERR "----------------------- send event: {Error: UNC IDNF ABORTED} --------------------\n");

		func_synobios_event_handler(SYNO_EVENT_DISK_TIMEOUT_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									0,
									0,
									iDiskPortType);

		printk(KERN_ERR "----------------------- sent event: {Timeout Others} --------------------\n");
		func_synobios_event_handler(SYNO_EVENT_DISK_TIMEOUT_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									1,
									0,
									iDiskPortType);
		printk(KERN_ERR "----------------------- sent event: {Timeout R/W} --------------------\n");

		func_synobios_event_handler(SYNO_EVENT_DISK_RESET_FAIL_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									0,
									iErrorCount,
									iDiskPortType);
		printk(KERN_ERR "----------------------- sent event: {Soft reset failed count %d} --------------------\n", iErrorCount);

		func_synobios_event_handler(SYNO_EVENT_DISK_RESET_FAIL_REPORT,
									6,
									iStartIdx,
									ap->nr_pmp_links,
									ata_dev->link->pmp,
									1,
									iErrorCount,
									iDiskPortType);
		printk(KERN_ERR "----------------------- sent event: {Hard reset failed count %d} --------------------\n", iErrorCount);

#ifdef MY_ABC_HERE
	/* slot index of PORT_LOST_XXX event should be 1 based. */
		iStartIdx = ap->syno_internal_slot_index;
		printk(KERN_ERR "----------------------- sent event: {Retry Failed} --------------------\n");
		func_synobios_event_handler(SYNO_EVENT_DISK_PORT_LOST, 2, iStartIdx + 1, PORT_LOST_RETRY_FAILED);
		func_synobios_event_handler(SYNO_EVENT_DISK_PORT_LOST, 2, iStartIdx + 1, PORT_LOST_RETRY_FAILED_PRESENT);
		printk(KERN_ERR "----------------------- sent event: {Port Disabled} --------------------\n");
		func_synobios_event_handler(SYNO_EVENT_DISK_PORT_LOST, 2, iStartIdx + 1, PORT_LOST_DISABLED);
		func_synobios_event_handler(SYNO_EVENT_DISK_PORT_LOST, 2, iStartIdx + 1, PORT_LOST_DISABLED_PRESENT);
		printk(KERN_ERR "----------------------- sent event: {link down} --------------------\n");
		func_synobios_event_handler(SYNO_EVENT_DISK_PORT_LOST, 2, iStartIdx + 1, PORT_LOST_LINK_DOWN_PRESENT);
#endif /* MY_ABC_HERE */

	}
	ret = count;

END:
	return ret;
}
DEVICE_ATTR(syno_sata_error_event_debug, S_IWUSR, NULL, syno_sata_error_event_debug_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_sata_error_event_debug);
#endif /* MY_ABC_HERE */

struct device_attribute *ata_common_sdev_attrs[] = {
	&dev_attr_unload_heads,
#ifdef MY_ABC_HERE
	&dev_attr_syno_wcache,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_deep_sleep_support,
	&dev_attr_syno_deep_sleep_ctrl,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_disk_latency_read_hist,
	&dev_attr_syno_disk_latency_write_hist,
	&dev_attr_syno_disk_latency_other_hist,
	&dev_attr_syno_disk_latency_stat,
#ifdef MY_ABC_HERE
	&dev_attr_syno_disk_seq_stat,
#endif /* MY_ABC_HERE */
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_pwr_reset_count,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_sata_error_event_debug,
#endif /* MY_ABC_HERE */
	NULL
};
EXPORT_SYMBOL_GPL(ata_common_sdev_attrs);

/**
 *	ata_std_bios_param - generic bios head/sector/cylinder calculator used by sd.
 *	@sdev: SCSI device for which BIOS geometry is to be determined
 *	@bdev: block device associated with @sdev
 *	@capacity: capacity of SCSI device
 *	@geom: location to which geometry will be output
 *
 *	Generic bios head/sector/cylinder calculator
 *	used by sd. Most BIOSes nowadays expect a XXX/255/16  (CHS)
 *	mapping. Some situations may arise where the disk is not
 *	bootable if this is not used.
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero.
 */
int ata_std_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		       sector_t capacity, int geom[])
{
	geom[0] = 255;
	geom[1] = 63;
	sector_div(capacity, 255*63);
	geom[2] = capacity;

	return 0;
}
EXPORT_SYMBOL_GPL(ata_std_bios_param);

/**
 *	ata_scsi_unlock_native_capacity - unlock native capacity
 *	@sdev: SCSI device to adjust device capacity for
 *
 *	This function is called if a partition on @sdev extends beyond
 *	the end of the device.  It requests EH to unlock HPA.
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  Might sleep.
 */
void ata_scsi_unlock_native_capacity(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	unsigned long flags;

	spin_lock_irqsave(ap->lock, flags);

	dev = ata_scsi_find_dev(ap, sdev);
	if (dev && dev->n_sectors < dev->n_native_sectors) {
		dev->flags |= ATA_DFLAG_UNLOCK_HPA;
		dev->link->eh_info.action |= ATA_EH_RESET;
		ata_port_schedule_eh(ap);
	}

	spin_unlock_irqrestore(ap->lock, flags);
	ata_port_wait_eh(ap);
}
EXPORT_SYMBOL_GPL(ata_scsi_unlock_native_capacity);

/**
 *	ata_get_identity - Handler for HDIO_GET_IDENTITY ioctl
 *	@ap: target port
 *	@sdev: SCSI device to get identify data for
 *	@arg: User buffer area for identify data
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */
static int ata_get_identity(struct ata_port *ap, struct scsi_device *sdev,
			    void __user *arg)
{
	struct ata_device *dev = ata_scsi_find_dev(ap, sdev);
	u16 __user *dst = arg;
	char buf[40];

	if (!dev)
		return -ENOMSG;

	if (copy_to_user(dst, dev->id, ATA_ID_WORDS * sizeof(u16)))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_PROD, ATA_ID_PROD_LEN);
	if (copy_to_user(dst + ATA_ID_PROD, buf, ATA_ID_PROD_LEN))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_FW_REV, ATA_ID_FW_REV_LEN);
	if (copy_to_user(dst + ATA_ID_FW_REV, buf, ATA_ID_FW_REV_LEN))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_SERNO, ATA_ID_SERNO_LEN);
	if (copy_to_user(dst + ATA_ID_SERNO, buf, ATA_ID_SERNO_LEN))
		return -EFAULT;

	return 0;
}

/**
 *	ata_cmd_ioctl - Handler for HDIO_DRIVE_CMD ioctl
 *	@scsidev: Device to which we are issuing command
 *	@arg: User provided data for issuing command
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */
int ata_cmd_ioctl(struct scsi_device *scsidev, void __user *arg)
{
	int rc = 0;
	u8 sensebuf[SCSI_SENSE_BUFFERSIZE];
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 args[4], *argbuf = NULL;
	int argsize = 0;
	enum dma_data_direction data_dir;
	struct scsi_sense_hdr sshdr;
	int cmd_result;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	memset(sensebuf, 0, sizeof(sensebuf));
	memset(scsi_cmd, 0, sizeof(scsi_cmd));

	if (args[3]) {
		argsize = ATA_SECT_SIZE * args[3];
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL) {
			rc = -ENOMEM;
			goto error;
		}

		scsi_cmd[1]  = (4 << 1); /* PIO Data-in */
		scsi_cmd[2]  = 0x0e;     /* no off.line or cc, read from dev,
					    block count in sector count field */
		data_dir = DMA_FROM_DEVICE;
	} else {
		scsi_cmd[1]  = (3 << 1); /* Non-data */
		scsi_cmd[2]  = 0x20;     /* cc but no off.line or data xfer */
		data_dir = DMA_NONE;
	}

	scsi_cmd[0] = ATA_16;

	scsi_cmd[4] = args[2];
	if (args[0] == ATA_CMD_SMART) { /* hack -- ide driver does this too */
		scsi_cmd[6]  = args[3];
		scsi_cmd[8]  = args[1];
		scsi_cmd[10] = ATA_SMART_LBAM_PASS;
		scsi_cmd[12] = ATA_SMART_LBAH_PASS;
	} else {
		scsi_cmd[6]  = args[1];
	}
	scsi_cmd[14] = args[0];

	/* Good values for timeout and retries?  Values below
	   from scsi_ioctl_send_command() for default case... */
	cmd_result = scsi_execute(scsidev, scsi_cmd, data_dir, argbuf, argsize,
				  sensebuf, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {/* sense data available */
		u8 *desc = sensebuf + 8;
		cmd_result &= ~(0xFF<<24); /* DRIVER_SENSE is not an error */

		/* If we set cc then ATA pass-through will cause a
		 * check condition even if no error. Filter that. */
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		/* Send userspace a few ATA registers (same as drivers/ide) */
		if (sensebuf[0] == 0x72 &&	/* format is "descriptor" */
		    desc[0] == 0x09) {		/* code is "ATA Descriptor" */
			args[0] = desc[13];	/* status */
			args[1] = desc[3];	/* error */
			args[2] = desc[5];	/* sector count (0:7) */
			if (copy_to_user(arg, args, sizeof(args)))
				rc = -EFAULT;
		}
	}


	if (cmd_result) {
		rc = -EIO;
		goto error;
	}

	if ((argbuf)
	 && copy_to_user(arg + sizeof(args), argbuf, argsize))
		rc = -EFAULT;
error:
	kfree(argbuf);
	return rc;
}

/**
 *	ata_task_ioctl - Handler for HDIO_DRIVE_TASK ioctl
 *	@scsidev: Device to which we are issuing command
 *	@arg: User provided data for issuing command
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */
int ata_task_ioctl(struct scsi_device *scsidev, void __user *arg)
{
	int rc = 0;
	u8 sensebuf[SCSI_SENSE_BUFFERSIZE];
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 args[7];
	struct scsi_sense_hdr sshdr;
	int cmd_result;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	memset(sensebuf, 0, sizeof(sensebuf));
	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	scsi_cmd[0]  = ATA_16;
	scsi_cmd[1]  = (3 << 1); /* Non-data */
	scsi_cmd[2]  = 0x20;     /* cc but no off.line or data xfer */
	scsi_cmd[4]  = args[1];
	scsi_cmd[6]  = args[2];
	scsi_cmd[8]  = args[3];
	scsi_cmd[10] = args[4];
	scsi_cmd[12] = args[5];
	scsi_cmd[13] = args[6] & 0x4f;
	scsi_cmd[14] = args[0];

	/* Good values for timeout and retries?  Values below
	   from scsi_ioctl_send_command() for default case... */
	cmd_result = scsi_execute(scsidev, scsi_cmd, DMA_NONE, NULL, 0,
				sensebuf, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {/* sense data available */
		u8 *desc = sensebuf + 8;
		cmd_result &= ~(0xFF<<24); /* DRIVER_SENSE is not an error */

		/* If we set cc then ATA pass-through will cause a
		 * check condition even if no error. Filter that. */
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			if (sshdr.sense_key == RECOVERED_ERROR &&
			    sshdr.asc == 0 && sshdr.ascq == 0x1d)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		/* Send userspace ATA registers */
		if (sensebuf[0] == 0x72 &&	/* format is "descriptor" */
				desc[0] == 0x09) {/* code is "ATA Descriptor" */
			args[0] = desc[13];	/* status */
			args[1] = desc[3];	/* error */
			args[2] = desc[5];	/* sector count (0:7) */
			args[3] = desc[7];	/* lbal */
			args[4] = desc[9];	/* lbam */
			args[5] = desc[11];	/* lbah */
			args[6] = desc[12];	/* select */
			if (copy_to_user(arg, args, sizeof(args)))
				rc = -EFAULT;
		}
	}

	if (cmd_result) {
		rc = -EIO;
		goto error;
	}

 error:
	return rc;
}

#ifdef MY_ABC_HERE
/**
  * This function is used to get SATA disk power status.
  *
  * @param scsidev    The SCSI device structure of the disk
  * @param DiskStatus We will put disk status in DiskStatus. If DiskStatus == 0,
  *                   means the disk is sleeping. If DiskStatus == 255, means
  *                   the disk is active.
  *
  * @return <0: Failed
  *         0: Success
  */
int SynoDiskPowerCheck(struct scsi_device *scsidev, int *DiskStatus)
{
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 sensebuf[SCSI_SENSE_BUFFERSIZE];
	int result = -EFAULT;
	struct scsi_sense_hdr sshdr;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));

	scsi_cmd[0] = ATA_16;
	scsi_cmd[1]  = (3 << 1); /* Non-data */

	/* So the ata_scsi_qc_complete() will call ata_gen_ata_desc_sense() to fill taskfile registers. */
	scsi_cmd[2] = 0x20;
	scsi_cmd[14] = ATA_CMD_CHK_POWER;

	memset(sensebuf, 0, sizeof(sensebuf));

	result = scsi_execute(scsidev, scsi_cmd, DMA_NONE, NULL, 0,
				sensebuf, &sshdr, (10*HZ), 5, 0, 0, NULL);

	if (result == ((DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION)) {
		*DiskStatus = sensebuf[13];
		result = 0;
	}

	return result;
}
#endif /* MY_ABC_HERE */

static int ata_ioc32(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_PIO_DMA)
		return 1;
	if (ap->pflags & ATA_PFLAG_PIO32)
		return 1;
	return 0;
}

/*
 * This handles both native and compat commands, so anything added
 * here must have a compatible argument, or check in_compat_syscall()
 */
int ata_sas_scsi_ioctl(struct ata_port *ap, struct scsi_device *scsidev,
		     unsigned int cmd, void __user *arg)
{
	unsigned long val;
	int rc = -EINVAL;
	unsigned long flags;
#ifdef MY_ABC_HERE
	struct ata_device *dev;
#endif /* MY_ABC_HERE */

	switch (cmd) {
	case HDIO_GET_32BIT:
		spin_lock_irqsave(ap->lock, flags);
		val = ata_ioc32(ap);
		spin_unlock_irqrestore(ap->lock, flags);
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			return put_user(val, (compat_ulong_t __user *)arg);
#endif
		return put_user(val, (unsigned long __user *)arg);

	case HDIO_SET_32BIT:
		val = (unsigned long) arg;
		rc = 0;
		spin_lock_irqsave(ap->lock, flags);
		if (ap->pflags & ATA_PFLAG_PIO32CHANGE) {
			if (val)
				ap->pflags |= ATA_PFLAG_PIO32;
			else
				ap->pflags &= ~ATA_PFLAG_PIO32;
		} else {
			if (val != ata_ioc32(ap))
				rc = -EINVAL;
		}
		spin_unlock_irqrestore(ap->lock, flags);
		return rc;

	case HDIO_GET_IDENTITY:
		return ata_get_identity(ap, scsidev, arg);

	case HDIO_DRIVE_CMD:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ata_cmd_ioctl(scsidev, arg);

	case HDIO_DRIVE_TASK:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ata_task_ioctl(scsidev, arg);

#ifdef MY_ABC_HERE
	case HDIO_GET_DMA:
		{
			dev = ata_scsi_find_dev(ap, scsidev);

			if(!dev)
				return -ENODEV;

			if (dev->xfer_mode <= XFER_PIO_4) {
				val = 0;
			} else {
				val = 1;
			}
			if (copy_to_user(arg, &val, sizeof(int)))
				return -EFAULT;
			return 0;
		}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	case ATA_CMD_CHK_POWER:
		{
			int *DiskStatus = (int *)arg;
			return SynoDiskPowerCheck(scsidev, DiskStatus);
		}
#endif /* MY_ABC_HERE */

	default:
		rc = -ENOTTY;
		break;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(ata_sas_scsi_ioctl);

int ata_scsi_ioctl(struct scsi_device *scsidev, unsigned int cmd,
		   void __user *arg)
{
	return ata_sas_scsi_ioctl(ata_shost_to_port(scsidev->host),
				scsidev, cmd, arg);
}
EXPORT_SYMBOL_GPL(ata_scsi_ioctl);

/**
 *	ata_scsi_qc_new - acquire new ata_queued_cmd reference
 *	@dev: ATA device to which the new command is attached
 *	@cmd: SCSI command that originated this ATA command
 *
 *	Obtain a reference to an unused ata_queued_cmd structure,
 *	which is the basic libata structure representing a single
 *	ATA command sent to the hardware.
 *
 *	If a command was available, fill in the SCSI-specific
 *	portions of the structure with information on the
 *	current command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Command allocated, or %NULL if none available.
 */
static struct ata_queued_cmd *ata_scsi_qc_new(struct ata_device *dev,
					      struct scsi_cmnd *cmd)
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new_init(dev, cmd->request->tag);
	if (qc) {
		qc->scsicmd = cmd;
		qc->scsidone = cmd->scsi_done;

		qc->sg = scsi_sglist(cmd);
		qc->n_elem = scsi_sg_count(cmd);

		if (cmd->request->rq_flags & RQF_QUIET)
			qc->flags |= ATA_QCFLAG_QUIET;
	} else {
		cmd->result = (DID_OK << 16) | (QUEUE_FULL << 1);
		cmd->scsi_done(cmd);
	}

	return qc;
}

static void ata_qc_set_pc_nbytes(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;

	qc->extrabytes = scmd->extra_len;
	qc->nbytes = scsi_bufflen(scmd) + qc->extrabytes;
}

/**
 *	ata_dump_status - user friendly display of error info
 *	@id: id of the port in question
 *	@tf: ptr to filled out taskfile
 *
 *	Decode and dump the ATA error/status registers for the user so
 *	that they have some idea what really happened at the non
 *	make-believe layer.
 *
 *	LOCKING:
 *	inherited from caller
 */
static void ata_dump_status(unsigned id, struct ata_taskfile *tf)
{
	u8 stat = tf->command, err = tf->feature;

	pr_warn("ata%u: status=0x%02x { ", id, stat);
	if (stat & ATA_BUSY) {
		pr_cont("Busy }\n");	/* Data is not valid in this case */
	} else {
		if (stat & ATA_DRDY)	pr_cont("DriveReady ");
		if (stat & ATA_DF)	pr_cont("DeviceFault ");
		if (stat & ATA_DSC)	pr_cont("SeekComplete ");
		if (stat & ATA_DRQ)	pr_cont("DataRequest ");
		if (stat & ATA_CORR)	pr_cont("CorrectedError ");
		if (stat & ATA_SENSE)	pr_cont("Sense ");
		if (stat & ATA_ERR)	pr_cont("Error ");
		pr_cont("}\n");

		if (err) {
			pr_warn("ata%u: error=0x%02x { ", id, err);
			if (err & ATA_ABORTED)	pr_cont("DriveStatusError ");
			if (err & ATA_ICRC) {
				if (err & ATA_ABORTED)
						pr_cont("BadCRC ");
				else		pr_cont("Sector ");
			}
			if (err & ATA_UNC)	pr_cont("UncorrectableError ");
			if (err & ATA_IDNF)	pr_cont("SectorIdNotFound ");
			if (err & ATA_TRK0NF)	pr_cont("TrackZeroNotFound ");
			if (err & ATA_AMNF)	pr_cont("AddrMarkNotFound ");
			pr_cont("}\n");
		}
	}
}

/**
 *	ata_to_sense_error - convert ATA error to SCSI error
 *	@id: ATA device number
 *	@drv_stat: value contained in ATA status register
 *	@drv_err: value contained in ATA error register
 *	@sk: the sense key we'll fill out
 *	@asc: the additional sense code we'll fill out
 *	@ascq: the additional sense code qualifier we'll fill out
 *	@verbose: be verbose
 *
 *	Converts an ATA error into a SCSI error.  Fill out pointers to
 *	SK, ASC, and ASCQ bytes for later use in fixed or descriptor
 *	format sense blocks.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void ata_to_sense_error(unsigned id, u8 drv_stat, u8 drv_err, u8 *sk,
			       u8 *asc, u8 *ascq, int verbose)
{
	int i;

	/* Based on the 3ware driver translation table */
	static const unsigned char sense_table[][4] = {
		/* BBD|ECC|ID|MAR */
		{0xd1,		ABORTED_COMMAND, 0x00, 0x00},
			// Device busy                  Aborted command
		/* BBD|ECC|ID */
		{0xd0,		ABORTED_COMMAND, 0x00, 0x00},
			// Device busy                  Aborted command
		/* ECC|MC|MARK */
		{0x61,		HARDWARE_ERROR, 0x00, 0x00},
			// Device fault                 Hardware error
		/* ICRC|ABRT */		/* NB: ICRC & !ABRT is BBD */
		{0x84,		ABORTED_COMMAND, 0x47, 0x00},
			// Data CRC error               SCSI parity error
		/* MC|ID|ABRT|TRK0|MARK */
		{0x37,		NOT_READY, 0x04, 0x00},
			// Unit offline                 Not ready
		/* MCR|MARK */
		{0x09,		NOT_READY, 0x04, 0x00},
			// Unrecovered disk error       Not ready
		/*  Bad address mark */
		{0x01,		MEDIUM_ERROR, 0x13, 0x00},
			// Address mark not found for data field
		/* TRK0 - Track 0 not found */
		{0x02,		HARDWARE_ERROR, 0x00, 0x00},
			// Hardware error
		/* Abort: 0x04 is not translated here, see below */
		/* Media change request */
		{0x08,		NOT_READY, 0x04, 0x00},
			// FIXME: faking offline
		/* SRV/IDNF - ID not found */
		{0x10,		ILLEGAL_REQUEST, 0x21, 0x00},
			// Logical address out of range
		/* MC - Media Changed */
		{0x20,		UNIT_ATTENTION, 0x28, 0x00},
			// Not ready to ready change, medium may have changed
		/* ECC - Uncorrectable ECC error */
		{0x40,		MEDIUM_ERROR, 0x11, 0x04},
			// Unrecovered read error
		/* BBD - block marked bad */
		{0x80,		MEDIUM_ERROR, 0x11, 0x04},
			// Block marked bad	Medium error, unrecovered read error
		{0xFF, 0xFF, 0xFF, 0xFF}, // END mark
	};
	static const unsigned char stat_table[][4] = {
		/* Must be first because BUSY means no other bits valid */
		{0x80,		ABORTED_COMMAND, 0x47, 0x00},
		// Busy, fake parity for now
		{0x40,		ILLEGAL_REQUEST, 0x21, 0x04},
		// Device ready, unaligned write command
		{0x20,		HARDWARE_ERROR,  0x44, 0x00},
		// Device fault, internal target failure
		{0x08,		ABORTED_COMMAND, 0x47, 0x00},
		// Timed out in xfer, fake parity for now
		{0x04,		RECOVERED_ERROR, 0x11, 0x00},
		// Recovered ECC error	  Medium error, recovered
		{0xFF, 0xFF, 0xFF, 0xFF}, // END mark
	};

	/*
	 *	Is this an error we can process/parse
	 */
	if (drv_stat & ATA_BUSY) {
		drv_err = 0;	/* Ignore the err bits, they're invalid */
	}

	if (drv_err) {
		/* Look for drv_err */
		for (i = 0; sense_table[i][0] != 0xFF; i++) {
			/* Look for best matches first */
			if ((sense_table[i][0] & drv_err) ==
			    sense_table[i][0]) {
				*sk = sense_table[i][1];
				*asc = sense_table[i][2];
				*ascq = sense_table[i][3];
				goto translate_done;
			}
		}
	}

	/*
	 * Fall back to interpreting status bits.  Note that if the drv_err
	 * has only the ABRT bit set, we decode drv_stat.  ABRT by itself
	 * is not descriptive enough.
	 */
	for (i = 0; stat_table[i][0] != 0xFF; i++) {
		if (stat_table[i][0] & drv_stat) {
			*sk = stat_table[i][1];
			*asc = stat_table[i][2];
			*ascq = stat_table[i][3];
			goto translate_done;
		}
	}

	/*
	 * We need a sensible error return here, which is tricky, and one
	 * that won't cause people to do things like return a disk wrongly.
	 */
	*sk = ABORTED_COMMAND;
	*asc = 0x00;
	*ascq = 0x00;

 translate_done:
	if (verbose)
		pr_err("ata%u: translated ATA stat/err 0x%02x/%02x to SCSI SK/ASC/ASCQ 0x%x/%02x/%02x\n",
		       id, drv_stat, drv_err, *sk, *asc, *ascq);
	return;
}

/*
 *	ata_gen_passthru_sense - Generate check condition sense block.
 *	@qc: Command that completed.
 *
 *	This function is specific to the ATA descriptor format sense
 *	block specified for the ATA pass through commands.  Regardless
 *	of whether the command errored or not, return a sense
 *	block. Copy all controller registers into the sense
 *	block. If there was no error, we get the request from an ATA
 *	passthrough command, so we use the following sense data:
 *	sk = RECOVERED ERROR
 *	asc,ascq = ATA PASS-THROUGH INFORMATION AVAILABLE
 *      
 *
 *	LOCKING:
 *	None.
 */
static void ata_gen_passthru_sense(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->result_tf;
	unsigned char *sb = cmd->sense_buffer;
	unsigned char *desc = sb + 8;
	int verbose = qc->ap->ops->error_handler == NULL;
	u8 sense_key, asc, ascq;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	/*
	 * Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (qc->err_mask ||
	    tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->print_id, tf->command, tf->feature,
				   &sense_key, &asc, &ascq, verbose);
		ata_scsi_set_sense(qc->dev, cmd, sense_key, asc, ascq);
	} else {
		/*
		 * ATA PASS-THROUGH INFORMATION AVAILABLE
		 * Always in descriptor format sense.
		 */
		scsi_build_sense_buffer(1, cmd->sense_buffer,
					RECOVERED_ERROR, 0, 0x1D);
	}

	if ((cmd->sense_buffer[0] & 0x7f) >= 0x72) {
		u8 len;

		/* descriptor format */
		len = sb[7];
		desc = (char *)scsi_sense_desc_find(sb, len + 8, 9);
		if (!desc) {
			if (SCSI_SENSE_BUFFERSIZE < len + 14)
				return;
			sb[7] = len + 14;
			desc = sb + 8 + len;
		}
		desc[0] = 9;
		desc[1] = 12;
		/*
		 * Copy registers into sense buffer.
		 */
		desc[2] = 0x00;
		desc[3] = tf->feature;	/* == error reg */
		desc[5] = tf->nsect;
		desc[7] = tf->lbal;
		desc[9] = tf->lbam;
		desc[11] = tf->lbah;
		desc[12] = tf->device;
		desc[13] = tf->command; /* == status reg */

		/*
		 * Fill in Extend bit, and the high order bytes
		 * if applicable.
		 */
		if (tf->flags & ATA_TFLAG_LBA48) {
			desc[2] |= 0x01;
			desc[4] = tf->hob_nsect;
			desc[6] = tf->hob_lbal;
			desc[8] = tf->hob_lbam;
			desc[10] = tf->hob_lbah;
		}
	} else {
		/* Fixed sense format */
		desc[0] = tf->feature;
		desc[1] = tf->command; /* status */
		desc[2] = tf->device;
		desc[3] = tf->nsect;
		desc[7] = 0;
		if (tf->flags & ATA_TFLAG_LBA48)  {
			desc[8] |= 0x80;
			if (tf->hob_nsect)
				desc[8] |= 0x40;
			if (tf->hob_lbal || tf->hob_lbam || tf->hob_lbah)
				desc[8] |= 0x20;
		}
		desc[9] = tf->lbal;
		desc[10] = tf->lbam;
		desc[11] = tf->lbah;
	}
}

/**
 *	ata_gen_ata_sense - generate a SCSI fixed sense block
 *	@qc: Command that we are erroring out
 *
 *	Generate sense block for a failed ATA command @qc.  Descriptor
 *	format is used to accommodate LBA48 block address.
 *
 *	LOCKING:
 *	None.
 */
static void ata_gen_ata_sense(struct ata_queued_cmd *qc)
{
	struct ata_device *dev = qc->dev;
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->result_tf;
	unsigned char *sb = cmd->sense_buffer;
	int verbose = qc->ap->ops->error_handler == NULL;
	u64 block;
	u8 sense_key, asc, ascq;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	if (ata_dev_disabled(dev)) {
		/* Device disabled after error recovery */
		/* LOGICAL UNIT NOT READY, HARD RESET REQUIRED */
		ata_scsi_set_sense(dev, cmd, NOT_READY, 0x04, 0x21);
		return;
	}
	/* Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (qc->err_mask ||
	    tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->print_id, tf->command, tf->feature,
				   &sense_key, &asc, &ascq, verbose);
		ata_scsi_set_sense(dev, cmd, sense_key, asc, ascq);
	} else {
		/* Could not decode error */
		ata_dev_warn(dev, "could not decode error status 0x%x err_mask 0x%x\n",
			     tf->command, qc->err_mask);
		ata_scsi_set_sense(dev, cmd, ABORTED_COMMAND, 0, 0);
		return;
	}

	block = ata_tf_read_block(&qc->result_tf, dev);
	if (block == U64_MAX)
		return;

	scsi_set_sense_information(sb, SCSI_SENSE_BUFFERSIZE, block);
}

void ata_scsi_sdev_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
	sdev->no_write_same = 1;

	/* Schedule policy is determined by ->qc_defer() callback and
	 * it needs to see every deferred qc.  Set dev_blocked to 1 to
	 * prevent SCSI midlayer from automatically deferring
	 * requests.
	 */
	sdev->max_device_blocked = 1;
#ifdef MY_ABC_HERE
	sdev->default_disable_fua = 1;
#endif /* MY_ABC_HERE */
}

/**
 *	ata_scsi_dma_need_drain - Check whether data transfer may overflow
 *	@rq: request to be checked
 *
 *	ATAPI commands which transfer variable length data to host
 *	might overflow due to application error or hardware bug.  This
 *	function checks whether overflow should be drained and ignored
 *	for @request.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if ; otherwise, 0.
 */
bool ata_scsi_dma_need_drain(struct request *rq)
{
	return atapi_cmd_type(scsi_req(rq)->cmd[0]) == ATAPI_MISC;
}
EXPORT_SYMBOL_GPL(ata_scsi_dma_need_drain);

int ata_scsi_dev_config(struct scsi_device *sdev, struct ata_device *dev)
{
	struct request_queue *q = sdev->request_queue;

	if (!ata_id_has_unload(dev->id))
		dev->flags |= ATA_DFLAG_NO_UNLOAD;

	/* configure max sectors */
	blk_queue_max_hw_sectors(q, dev->max_sectors);

	if (dev->class == ATA_DEV_ATAPI) {
		sdev->sector_size = ATA_SECT_SIZE;

		/* set DMA padding */
		blk_queue_update_dma_pad(q, ATA_DMA_PAD_SZ - 1);

		/* make room for appending the drain */
		blk_queue_max_segments(q, queue_max_segments(q) - 1);

		sdev->dma_drain_len = ATAPI_MAX_DRAIN;
		sdev->dma_drain_buf = kmalloc(sdev->dma_drain_len,
				q->bounce_gfp | GFP_KERNEL);
		if (!sdev->dma_drain_buf) {
			ata_dev_err(dev, "drain buffer allocation failed\n");
			return -ENOMEM;
		}
	} else {
		sdev->sector_size = ata_id_logical_sector_size(dev->id);
		sdev->manage_start_stop = 1;

#ifdef MY_ABC_HERE
		if (iIsSynoPmCtlSupport(dev->link->ap)) {
			sdev->power_loss_during_reboot = 1;
		}
#endif /* MY_ABC_HERE */
	}

	/*
	 * ata_pio_sectors() expects buffer for each sector to not cross
	 * page boundary.  Enforce it by requiring buffers to be sector
	 * aligned, which works iff sector_size is not larger than
	 * PAGE_SIZE.  ATAPI devices also need the alignment as
	 * IDENTIFY_PACKET is executed as ATA_PROT_PIO.
	 */
	if (sdev->sector_size > PAGE_SIZE)
		ata_dev_warn(dev,
			"sector_size=%u > PAGE_SIZE, PIO may malfunction\n",
			sdev->sector_size);

	blk_queue_update_dma_alignment(q, sdev->sector_size - 1);

	if (dev->flags & ATA_DFLAG_AN)
		set_bit(SDEV_EVT_MEDIA_CHANGE, sdev->supported_events);

	if (dev->flags & ATA_DFLAG_NCQ) {
		int depth;

		depth = min(sdev->host->can_queue, ata_id_queue_depth(dev->id));
		depth = min(ATA_MAX_QUEUE, depth);
		scsi_change_queue_depth(sdev, depth);
	}

	if (dev->flags & ATA_DFLAG_TRUSTED)
		sdev->security_supported = 1;

	dev->sdev = sdev;
	return 0;
}

/**
 *	ata_scsi_slave_config - Set SCSI device attributes
 *	@sdev: SCSI device to examine
 *
 *	This is called before we actually start reading
 *	and writing to the device, to configure certain
 *	SCSI mid-layer behaviors.
 *
 *	LOCKING:
 *	Defined by SCSI layer.  We don't really care.
 */

int ata_scsi_slave_config(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev = __ata_scsi_find_dev(ap, sdev);
	int rc = 0;

	ata_scsi_sdev_config(sdev);

	if (dev)
		rc = ata_scsi_dev_config(sdev, dev);

	return rc;
}
EXPORT_SYMBOL_GPL(ata_scsi_slave_config);

/**
 *	ata_scsi_slave_destroy - SCSI device is about to be destroyed
 *	@sdev: SCSI device to be destroyed
 *
 *	@sdev is about to be destroyed for hot/warm unplugging.  If
 *	this unplugging was initiated by libata as indicated by NULL
 *	dev->sdev, this function doesn't have to do anything.
 *	Otherwise, SCSI layer initiated warm-unplug is in progress.
 *	Clear dev->sdev, schedule the device for ATA detach and invoke
 *	EH.
 *
 *	LOCKING:
 *	Defined by SCSI layer.  We don't really care.
 */
void ata_scsi_slave_destroy(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	unsigned long flags;
	struct ata_device *dev;

	if (!ap->ops->error_handler)
		return;

	spin_lock_irqsave(ap->lock, flags);
	dev = __ata_scsi_find_dev(ap, sdev);
	if (dev && dev->sdev) {
		/* SCSI device already in CANCEL state, no need to offline it */
		dev->sdev = NULL;
		dev->flags |= ATA_DFLAG_DETACH;
		ata_port_schedule_eh(ap);
	}
	spin_unlock_irqrestore(ap->lock, flags);

	kfree(sdev->dma_drain_buf);
}
EXPORT_SYMBOL_GPL(ata_scsi_slave_destroy);

/**
 *	ata_scsi_start_stop_xlat - Translate SCSI START STOP UNIT command
 *	@qc: Storage for translated ATA taskfile
 *
 *	Sets up an ATA taskfile to issue STANDBY (to stop) or READ VERIFY
 *	(to start). Perhaps these commands should be preceded by
 *	CHECK POWER MODE to see what power mode the device is already in.
 *	[See SAT revision 5 at www.t10.org]
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_start_stop_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	const u8 *cdb = scmd->cmnd;
	u16 fp;
	u8 bp = 0xff;

	if (scmd->cmd_len < 5) {
		fp = 4;
		goto invalid_fld;
	}

	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf->protocol = ATA_PROT_NODATA;
	if (cdb[1] & 0x1) {
		;	/* ignore IMMED bit, violates sat-r05 */
	}
	if (cdb[4] & 0x2) {
		fp = 4;
		bp = 1;
		goto invalid_fld;       /* LOEJ bit set not supported */
	}
	if (((cdb[4] >> 4) & 0xf) != 0) {
		fp = 4;
		bp = 3;
		goto invalid_fld;       /* power conditions not supported */
	}

	if (cdb[4] & 0x1) {
		tf->nsect = 1;	/* 1 sector, lba=0 */

		if (qc->dev->flags & ATA_DFLAG_LBA) {
			tf->flags |= ATA_TFLAG_LBA;

			tf->lbah = 0x0;
			tf->lbam = 0x0;
			tf->lbal = 0x0;
			tf->device |= ATA_LBA;
		} else {
			/* CHS */
			tf->lbal = 0x1; /* sect */
			tf->lbam = 0x0; /* cyl low */
			tf->lbah = 0x0; /* cyl high */
		}

		tf->command = ATA_CMD_VERIFY;	/* READ VERIFY */
	} else {
		/* Some odd clown BIOSen issue spindown on power off (ACPI S4
		 * or S5) causing some drives to spin up and down again.
		 */
		if ((qc->ap->flags & ATA_FLAG_NO_POWEROFF_SPINDOWN) &&
		    system_state == SYSTEM_POWER_OFF)
			goto skip;

		if ((qc->ap->flags & ATA_FLAG_NO_HIBERNATE_SPINDOWN) &&
		     system_entering_hibernation())
			goto skip;

		/* Issue ATA STANDBY IMMEDIATE command */
		tf->command = ATA_CMD_STANDBYNOW1;
	}

	/*
	 * Standby and Idle condition timers could be implemented but that
	 * would require libata to implement the Power condition mode page
	 * and allow the user to change it. Changing mode pages requires
	 * MODE SELECT to be implemented.
	 */

	return 0;

 invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, bp);
	return 1;
 skip:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}


/**
 *	ata_scsi_flush_xlat - Translate SCSI SYNCHRONIZE CACHE command
 *	@qc: Storage for translated ATA taskfile
 *
 *	Sets up an ATA taskfile to issue FLUSH CACHE or
 *	FLUSH CACHE EXT.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_flush_xlat(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if (qc->dev->flags & ATA_DFLAG_FLUSH_EXT)
		tf->command = ATA_CMD_FLUSH_EXT;
	else
		tf->command = ATA_CMD_FLUSH;

	/* flush is critical for IO integrity, consider it an IO command */
	qc->flags |= ATA_QCFLAG_IO;

	return 0;
}

/**
 *	scsi_6_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 6-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_6_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len;

	VPRINTK("six-byte command\n");

	lba |= ((u64)(cdb[1] & 0x1f)) << 16;
	lba |= ((u64)cdb[2]) << 8;
	lba |= ((u64)cdb[3]);

	len = cdb[4];

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_10_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 10-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_10_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("ten-byte command\n");

	lba |= ((u64)cdb[2]) << 24;
	lba |= ((u64)cdb[3]) << 16;
	lba |= ((u64)cdb[4]) << 8;
	lba |= ((u64)cdb[5]);

	len |= ((u32)cdb[7]) << 8;
	len |= ((u32)cdb[8]);

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_16_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 16-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_16_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("sixteen-byte command\n");

	lba |= ((u64)cdb[2]) << 56;
	lba |= ((u64)cdb[3]) << 48;
	lba |= ((u64)cdb[4]) << 40;
	lba |= ((u64)cdb[5]) << 32;
	lba |= ((u64)cdb[6]) << 24;
	lba |= ((u64)cdb[7]) << 16;
	lba |= ((u64)cdb[8]) << 8;
	lba |= ((u64)cdb[9]);

	len |= ((u32)cdb[10]) << 24;
	len |= ((u32)cdb[11]) << 16;
	len |= ((u32)cdb[12]) << 8;
	len |= ((u32)cdb[13]);

	*plba = lba;
	*plen = len;
}

/**
 *	ata_scsi_verify_xlat - Translate SCSI VERIFY command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *
 *	Converts SCSI VERIFY command to an ATA READ VERIFY command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_verify_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;
	u64 dev_sectors = qc->dev->n_sectors;
	const u8 *cdb = scmd->cmnd;
	u64 block;
	u32 n_block;
	u16 fp;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if (cdb[0] == VERIFY) {
		if (scmd->cmd_len < 10) {
			fp = 9;
			goto invalid_fld;
		}
		scsi_10_lba_len(cdb, &block, &n_block);
	} else if (cdb[0] == VERIFY_16) {
		if (scmd->cmd_len < 16) {
			fp = 15;
			goto invalid_fld;
		}
		scsi_16_lba_len(cdb, &block, &n_block);
	} else {
		fp = 0;
		goto invalid_fld;
	}

	if (!n_block)
		goto nothing_to_do;
	if (block >= dev_sectors)
		goto out_of_range;
	if ((block + n_block) > dev_sectors)
		goto out_of_range;

	if (dev->flags & ATA_DFLAG_LBA) {
		tf->flags |= ATA_TFLAG_LBA;

		if (lba_28_ok(block, n_block)) {
			/* use LBA28 */
			tf->command = ATA_CMD_VERIFY;
			tf->device |= (block >> 24) & 0xf;
		} else if (lba_48_ok(block, n_block)) {
			if (!(dev->flags & ATA_DFLAG_LBA48))
				goto out_of_range;

			/* use LBA48 */
			tf->flags |= ATA_TFLAG_LBA48;
			tf->command = ATA_CMD_VERIFY_EXT;

			tf->hob_nsect = (n_block >> 8) & 0xff;

			tf->hob_lbah = (block >> 40) & 0xff;
			tf->hob_lbam = (block >> 32) & 0xff;
			tf->hob_lbal = (block >> 24) & 0xff;
		} else
			/* request too large even for LBA48 */
			goto out_of_range;

		tf->nsect = n_block & 0xff;

		tf->lbah = (block >> 16) & 0xff;
		tf->lbam = (block >> 8) & 0xff;
		tf->lbal = block & 0xff;

		tf->device |= ATA_LBA;
	} else {
		/* CHS */
		u32 sect, head, cyl, track;

		if (!lba_28_ok(block, n_block))
			goto out_of_range;

		/* Convert LBA to CHS */
		track = (u32)block / dev->sectors;
		cyl   = track / dev->heads;
		head  = track % dev->heads;
		sect  = (u32)block % dev->sectors + 1;

		DPRINTK("block %u track %u cyl %u head %u sect %u\n",
			(u32)block, track, cyl, head, sect);

		/* Check whether the converted CHS can fit.
		   Cylinder: 0-65535
		   Head: 0-15
		   Sector: 1-255*/
		if ((cyl >> 16) || (head >> 4) || (sect >> 8) || (!sect))
			goto out_of_range;

		tf->command = ATA_CMD_VERIFY;
		tf->nsect = n_block & 0xff; /* Sector count 0 means 256 sectors */
		tf->lbal = sect;
		tf->lbam = cyl;
		tf->lbah = cyl >> 8;
		tf->device |= head;
	}

	return 0;

invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, 0xff);
	return 1;

out_of_range:
	ata_scsi_set_sense(qc->dev, scmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}

static bool ata_check_nblocks(struct scsi_cmnd *scmd, u32 n_blocks)
{
	struct request *rq = scmd->request;
	u32 req_blocks;

	if (!blk_rq_is_passthrough(rq))
		return true;

	req_blocks = blk_rq_bytes(rq) / scmd->device->sector_size;
	if (n_blocks > req_blocks)
		return false;

	return true;
}

/**
 *	ata_scsi_rw_xlat - Translate SCSI r/w command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *
 *	Converts any of six SCSI read/write commands into the
 *	ATA counterpart, including starting sector (LBA),
 *	sector count, and taking into account the device's LBA48
 *	support.
 *
 *	Commands %READ_6, %READ_10, %READ_16, %WRITE_6, %WRITE_10, and
 *	%WRITE_16 are currently supported.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_rw_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	struct request *rq = scmd->request;
	int class = IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
	unsigned int tf_flags = 0;
	u64 block;
	u32 n_block;
	int rc;
	u16 fp = 0;

#ifdef MY_ABC_HERE
	if (cdb[0] == WRITE_10 || cdb[0] == WRITE_6 || cdb[0] == WRITE_16) {
		tf_flags |= ATA_TFLAG_WRITE;
		qc->qc_stat.u8QcType = 2;
	} else if (cdb[0] == READ_10 || cdb[0] == READ_6 || cdb[0] == READ_16) {
		qc->qc_stat.u8QcType = 1;
	}
#else /* MY_ABC_HERE */
	if (cdb[0] == WRITE_10 || cdb[0] == WRITE_6 || cdb[0] == WRITE_16)
		tf_flags |= ATA_TFLAG_WRITE;
#endif /* MY_ABC_HERE */

	/* Calculate the SCSI LBA, transfer length and FUA. */
	switch (cdb[0]) {
	case READ_10:
	case WRITE_10:
		if (unlikely(scmd->cmd_len < 10)) {
			fp = 9;
			goto invalid_fld;
		}
		scsi_10_lba_len(cdb, &block, &n_block);
		if (cdb[1] & (1 << 3))
			tf_flags |= ATA_TFLAG_FUA;
		if (!ata_check_nblocks(scmd, n_block))
			goto invalid_fld;
		break;
	case READ_6:
	case WRITE_6:
		if (unlikely(scmd->cmd_len < 6)) {
			fp = 5;
			goto invalid_fld;
		}
		scsi_6_lba_len(cdb, &block, &n_block);

		/* for 6-byte r/w commands, transfer length 0
		 * means 256 blocks of data, not 0 block.
		 */
		if (!n_block)
			n_block = 256;
		if (!ata_check_nblocks(scmd, n_block))
			goto invalid_fld;
		break;
	case READ_16:
	case WRITE_16:
		if (unlikely(scmd->cmd_len < 16)) {
			fp = 15;
			goto invalid_fld;
		}
		scsi_16_lba_len(cdb, &block, &n_block);
		if (cdb[1] & (1 << 3))
			tf_flags |= ATA_TFLAG_FUA;
		if (!ata_check_nblocks(scmd, n_block))
			goto invalid_fld;
		break;
	default:
		DPRINTK("no-byte command\n");
		fp = 0;
		goto invalid_fld;
	}

	/* Check and compose ATA command */
	if (!n_block)
		/* For 10-byte and 16-byte SCSI R/W commands, transfer
		 * length 0 means transfer 0 block of data.
		 * However, for ATA R/W commands, sector count 0 means
		 * 256 or 65536 sectors, not 0 sectors as in SCSI.
		 *
		 * WARNING: one or two older ATA drives treat 0 as 0...
		 */
		goto nothing_to_do;

	qc->flags |= ATA_QCFLAG_IO;
	qc->nbytes = n_block * scmd->device->sector_size;
#ifdef MY_ABC_HERE
	qc->qc_stat.u64StartLbaByte = block * scmd->device->sector_size;
	qc->qc_stat.u8LbaZone = (block >> qc->dev->u8LbaZoneShiftBit) &
		SYNO_SEQ_SAMPLE_LBA_ZONE_MASK;
#endif /* MY_ABC_HERE */

	rc = ata_build_rw_tf(&qc->tf, qc->dev, block, n_block, tf_flags,
			     qc->hw_tag, class);

	if (likely(rc == 0))
		return 0;

	if (rc == -ERANGE)
		goto out_of_range;
	/* treat all other errors as -EINVAL, fall through */
invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, 0xff);
	return 1;

out_of_range:
	ata_scsi_set_sense(qc->dev, scmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}

static void ata_qc_done(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	void (*done)(struct scsi_cmnd *) = qc->scsidone;

	ata_qc_free(qc);
	done(cmd);
}

static void ata_scsi_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;
	u8 *cdb = cmd->cmnd;
#ifdef MY_ABC_HERE
	u8 *desc = NULL;
#endif /* MY_ABC_HERE */
	int need_sense = (qc->err_mask != 0);

	/* For ATA pass thru (SAT) commands, generate a sense block if
	 * user mandated it or if there's an error.  Note that if we
	 * generate because the user forced us to [CK_COND =1], a check
	 * condition is generated and the ATA register values are returned
	 * whether the command completed successfully or not. If there
	 * was no error, we use the following sense data:
	 * sk = RECOVERED ERROR
	 * asc,ascq = ATA PASS-THROUGH INFORMATION AVAILABLE
	 */
	if (((cdb[0] == ATA_16) || (cdb[0] == ATA_12)) &&
	    ((cdb[2] & 0x20) || need_sense))
		ata_gen_passthru_sense(qc);
	else if (qc->flags & ATA_QCFLAG_SENSE_VALID)
		cmd->result = SAM_STAT_CHECK_CONDITION;
#ifdef MY_ABC_HERE
	else if (need_sense) {
		ata_gen_ata_sense(qc);
		/* Only UNC errors need remaping, and we also make sure that
		 * the result is reported by log page 10h for NCQ commands.
		 * This prevents remapping with untrusted LBAs.
		 */
		if ((qc->result_tf.feature & ATA_UNC) &&
		    ata_is_ncq(qc->tf.protocol) &&
		    !(qc->err_mask & AC_ERR_NCQ)) {
			desc = qc->scsicmd->sense_buffer + 8;
			desc[SYNO_DESCRIPTOR_RESERVED_INDEX] |= SYNO_NCQ_FAKE_UNC;
		}
	}
#else /* MY_ABC_HERE */
	else if (need_sense)
		ata_gen_ata_sense(qc);
#endif /* MY_ABC_HERE */
	else
		cmd->result = SAM_STAT_GOOD;

	if (need_sense && !ap->ops->error_handler)
		ata_dump_status(ap->print_id, &qc->result_tf);

#ifdef MY_DEF_HERE
	if (!(cdb[0] == ATA_16 && cdb[14] == ATA_CMD_CHK_POWER)) {
		/* update time of last command */
		qc->dev->ulLastCmd = jiffies;
	}

	if ((cdb[0] == ATA_16) &&
		(ATA_CMD_IDLEIMMEDIATE == qc->tf.command ||
		 ATA_CMD_STANDBY == qc->tf.command ||
		 ATA_CMD_STANDBYNOW1 == qc->tf.command)) {
		DBGMESG("disk %d set iCheckPwr\n", ap->print_id);
		qc->dev->iCheckPwr = 1;
		if (0 < giSynoSpinupGroupNum) {
			gCurrentSpinupGroupNum = 0;
			guiWakeupDisksNum = giSynoSpinupGroup[0];
			/* reset giNeedWakeAll when no HDD waking*/
			giNeedWakeAll = 0;
		}
	}
#endif /* MY_DEF_HERE */

	ata_qc_done(qc);
}

/**
 *	ata_scsi_translate - Translate then issue SCSI command to ATA device
 *	@dev: ATA device to which the command is addressed
 *	@cmd: SCSI command to execute
 *	@xlat_func: Actor which translates @cmd to an ATA taskfile
 *
 *	Our ->queuecommand() function has decided that the SCSI
 *	command issued can be directly translated into an ATA
 *	command, rather than handled internally.
 *
 *	This function sets up an ata_queued_cmd structure for the
 *	SCSI command, and sends that ata_queued_cmd to the hardware.
 *
 *	The xlat_func argument (actor) returns 0 if ready to execute
 *	ATA command, else 1 to finish translation. If 1 is returned
 *	then cmd->result (and possibly cmd->sense_buffer) are assumed
 *	to be set reflecting an error condition or clean (early)
 *	termination.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	0 on success, SCSI_ML_QUEUE_DEVICE_BUSY if the command
 *	needs to be deferred.
 */
static int ata_scsi_translate(struct ata_device *dev, struct scsi_cmnd *cmd,
			      ata_xlat_func_t xlat_func)
{
	struct ata_port *ap = dev->link->ap;
	struct ata_queued_cmd *qc;
	int rc;

	VPRINTK("ENTER\n");

	qc = ata_scsi_qc_new(dev, cmd);
	if (!qc)
		goto err_mem;

	/* data is present; dma-map it */
	if (cmd->sc_data_direction == DMA_FROM_DEVICE ||
	    cmd->sc_data_direction == DMA_TO_DEVICE) {
		if (unlikely(scsi_bufflen(cmd) < 1)) {
			ata_dev_warn(dev, "WARNING: zero len r/w req\n");
			goto err_did;
		}

		ata_sg_init(qc, scsi_sglist(cmd), scsi_sg_count(cmd));

		qc->dma_dir = cmd->sc_data_direction;
	}

	qc->complete_fn = ata_scsi_qc_complete;

	if (xlat_func(qc))
		goto early_finish;

	if (ap->ops->qc_defer) {
		if ((rc = ap->ops->qc_defer(qc)))
			goto defer;
	}

	/* select device, send command to hardware */
	ata_qc_issue(qc);

	VPRINTK("EXIT\n");
	return 0;

early_finish:
	ata_qc_free(qc);
	cmd->scsi_done(cmd);
	DPRINTK("EXIT - early finish (good or error)\n");
	return 0;

err_did:
	ata_qc_free(qc);
	cmd->result = (DID_ERROR << 16);
	cmd->scsi_done(cmd);
err_mem:
	DPRINTK("EXIT - internal\n");
	return 0;

defer:
	ata_qc_free(qc);
	DPRINTK("EXIT - defer\n");
	if (rc == ATA_DEFER_LINK)
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else
		return SCSI_MLQUEUE_HOST_BUSY;
}

#ifdef MY_DEF_HERE
void ata_qc_complete_read(struct ata_queued_cmd *qc)
{
	if (qc->err_mask) {
		DBGMESG("read cmd qc->err_mask != 0 print_id %u pmp %u\n", qc->ap->print_id, qc->dev->link->pmp);
	}
	if (qc->flags & ATA_QCFLAG_FAILED) {
		DBGMESG("This read  qc is failed 0 print_id %u pmp %u\n", qc->ap->print_id, qc->dev->link->pmp);
	}

	DBGMESG("port %d clear CHKPOWER_FIRST_WAIT\n", qc->ap->print_id);
	clear_bit(CHKPOWER_FIRST_WAIT, &(qc->dev->ulSpinupState));

	if(NULL == qc->cursg) {
		printk(KERN_ERR "MEMORY LEAK!! qc->cursg is NULL, the psg we allocated becomes orphan \n");
		WARN_ON(1);
		goto OUT;
	}
	kfree(qc->cursg);

OUT:
	ata_qc_free(qc);
}

static int SynoIssueWakeUpCmd(struct ata_device *dev, struct scsi_cmnd *cmd)
{
	struct ata_queued_cmd *qc;
	struct ata_port *ap = dev->link->ap;
	struct scatterlist *psg = NULL;
	int rc;
	u16 *buf = (void *)dev->link->ap->sector_buf;

	if (test_and_set_bit(CHKPOWER_FIRST_WAIT, &(dev->ulSpinupState))) {
		printk("%s: there is already read cmd processing print_id %d link->pmp %d\n",
			   __FUNCTION__, ap->print_id, dev->link->pmp);
		WARN_ON(1);
		goto ERR_MEM;
	}

	/* issue a chk_power ata command to check disk power status */
	qc = ata_qc_new_init(dev, cmd->request->tag);
	if (NULL == qc) {
		DBGMESG("%s: read cmd fail NULL == qc print_id %d link->pmp %d\n",
			   __FUNCTION__, ap->print_id, dev->link->pmp);
		clear_bit(CHKPOWER_FIRST_WAIT, &(dev->ulSpinupState));
		goto ERR_MEM;
	}

	/* copy from ata_scsi_rw_xlat(..) and ata_exec_internal(..) */
	psg = kmalloc(ATA_SECT_SIZE, GFP_ATOMIC);//will free in complete function
	sg_init_one(psg, buf, ATA_SECT_SIZE);
	ata_sg_init(qc, psg, 1);
	/*
	 * for ASMEDIA 1061, SynoRead command always fail,
	 * so we change this to idle immediate command
	 * this command apply to ASMEDIA & RTDSoC HDD both.
	 * no need for init qc->nbytes & dma_dir & flags
	 * the init procedure is in ata_qc_new_init
	 */
	qc->tf.command = ATA_CMD_IDLEIMMEDIATE;
	qc->tf.flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	qc->tf.protocol = ATA_PROT_NODATA;
	qc->flags |= ATA_QCFLAG_RESULT_TF;
	qc->dma_dir = DMA_NONE;
	qc->complete_fn = ata_qc_complete_read;

	if (ap->ops->qc_defer) {
		if ((rc = ap->ops->qc_defer(qc))){
			/* if this port need defer, we should set CHKPOWER_FIRST_CMD and clear CHKPOWER_FIRST_WAIT
			 * to let this port re-insert read later */
			set_bit(CHKPOWER_FIRST_CMD, &(dev->ulSpinupState));
			clear_bit(CHKPOWER_FIRST_WAIT, &(dev->ulSpinupState));
			DBGMESG("%s read cmd qc_defer, print_id %d pmp %d tag %d\n", __FUNCTION__, ap->print_id, dev->link->pmp, qc->tag);
			goto DEFER;
		}
	}

	/* issue read and update gulLastWake */
	spin_lock(&SYNOLastWakeLock);
	gulLastWake = jiffies;
	/* count waking disks */
	++giWakingDisks;
	/* if all disks in group were waking, reset group */
	if (giWakingDisks == guiWakeupDisksNum) {
		giWakingDisks = giGroupDisks = 0;
		if(0 < giSynoSpinupGroupNum){
			DBG_SpinupGroup("Disk Group %d is full, going to delay for spinup.\n",gCurrentSpinupGroupNum);
			gCurrentSpinupGroupNum++;
			if (gCurrentSpinupGroupNum >= giSynoSpinupGroupNum) {
				/* if syno_spinup_group not use all disks, left hdd poweron 1 by 1 */
				guiWakeupDisksNum = 1;
			} else {
				guiWakeupDisksNum = giSynoSpinupGroup[gCurrentSpinupGroupNum];
			}
		}
	}
	spin_unlock(&SYNOLastWakeLock);
	DBGMESG("port %d update gulLastWake %lu and issue read\n", ap->print_id, gulLastWake);
	dev->ulLastCmd = jiffies;
	ata_qc_issue(qc);

	return SCSI_MLQUEUE_HOST_BUSY;

ERR_MEM:
	dev->ulLastCmd = jiffies;
	return SCSI_MLQUEUE_HOST_BUSY;
DEFER:
	ata_qc_free(qc);
	if (rc == ATA_DEFER_LINK)
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else
		return SCSI_MLQUEUE_HOST_BUSY;
}

static int syno_ata_scsi_translate(struct ata_device *dev, struct scsi_cmnd *cmd,
			      ata_xlat_func_t xlat_func)
{
	struct ata_port *ap = dev->link->ap;
	u8 *scsicmd = cmd->cmnd;
	int iNeedWait = 0;

	/* no insert comamnd while the device is derived from PM */
	if (ap->nr_pmp_links) {
		goto PASS;
	}

	/* no insert command while frozen */
	if (ap->pflags & ATA_PFLAG_FROZEN) {
		if (printk_ratelimit()) {
			DBGMESG("port %d ATA_PFLAG_FROZEN or ATA_FLAG_DISABLED, clear all bits\n", ap->print_id);
		}
		ata_port_schedule_eh(ap);
		clear_bit(CHKPOWER_FIRST_CMD, &(dev->ulSpinupState));
		clear_bit(CHKPOWER_FIRST_WAIT, &(dev->ulSpinupState));
		goto PASS;
	}

#ifdef MY_ABC_HERE
	if (dev->is_ssd) {
		goto PASS;
	}
#endif /* MY_ABC_HERE */

	/* The inserted command timeout! reset the port and notify upper layer to re-send command */
	if (test_bit(CHKPOWER_FIRST_WAIT, &(dev->ulSpinupState))) {
		if (time_after(jiffies, dev->ulLastCmd + ISSUEREADTIMEOUT)) {
			ata_link_printk(dev->link, KERN_ERR, "checking issue READ timeout\n");
			WARN_ON(1 != dev->link->ap->nr_active_links);
			dev->link->eh_info.action |= ATA_EH_RESET;
			ata_port_schedule_eh(dev->link->ap);
		}
		goto WAIT;
	}

	/* if already have ata command executing, don't insert ATA_CMD_CHK_POWER */
	if(0 != ap->nr_active_links) {
		goto PASS;
	}

	/* The ATA_CMD_CHK_POWER command won't wake up disk. So we don't check whether
	 * DS is sleeping now.
	 */
	if (scsicmd[0] == ATA_16 && scsicmd[14] == ATA_CMD_CHK_POWER) {
		goto PASS_ONCE;
	} else {
		/* we need insert read as the first cmd to wakeup disk */
		if (dev->iCheckPwr || test_bit(CHKPOWER_FIRST_CMD, &(dev->ulSpinupState))) {
			/* check if this port need wait other disks wakeup */
			spin_lock(&SYNOLastWakeLock);
			/* last hdd of group cannot reset group unless giSynoSpinupGroupNum = 0 */
			if(gulLastWake && time_after(jiffies, gulLastWake + SynoWakeInterval())) {
				if(0 < giSynoSpinupGroupNum && (giGroupDisks && giGroupDisks < guiWakeupDisksNum)) {
					giWakingDisks = giGroupDisks = 0;
				}
				else if(0 == giSynoSpinupGroupNum) {
					/* not set syno_spinup_group so go the original way*/
					giWakingDisks = giGroupDisks = 0;
				}
			}

			/* The following case, we can add this disk to group to wakup
			 * 1. No body waking
			 * 2. The group is empty and jiffies is already after last wakeup jiffies
			 * 3. The group not full
			 * 4. Dynamic HDD power on mechanism when two RP is connected.
			 **/
			if (!gulLastWake ||
				(!giGroupDisks &&
				 time_after(jiffies, gulLastWake + SynoWakeInterval())) ||
				 giNeedWakeAll ||
				(giGroupDisks && giGroupDisks < guiWakeupDisksNum)) {
				if (0 < giSynoSpinupGroupNum) {
					DBGMESG("hiberation debug: port %d detected\n", ap->print_id);
					if (SynoHaveRPDetectPin() && SynoAllRedundantPowerDetected()) {
						/* set giNeedWakeAll when this model have RP detect pin and two RP is power good*/
						giNeedWakeAll = 1;
					}
				}
				++giGroupDisks;
			} else {
				/* the group is full, must wait */
				iNeedWait = 1;
			}
			spin_unlock(&SYNOLastWakeLock);

			if (!iNeedWait) {
				goto ISSUE_READ;
			} else {
				/* These msg will appear very much, so we mark it.
				 * But it is useful for debug, I leave it here */
				if (printk_ratelimit()) {
					DBGMESG("port %d too close to last wakeup, wait again (%lu) (%lu) (%lu)\n",
							ap->print_id, jiffies, gulLastWake, SynoWakeInterval());
				}
				goto WAIT;
			}
		}
	}

PASS:
	dev->iCheckPwr = 0;
PASS_ONCE:
	/* update time-bookkeeping of last command */
	dev->ulLastCmd = jiffies;
	return ata_scsi_translate(dev, cmd, xlat_func);
ISSUE_READ:
	dev->iCheckPwr = 0;
	dev->ulSpinupState = 0;
	return SynoIssueWakeUpCmd(dev, cmd);
WAIT:
	return SCSI_MLQUEUE_HOST_BUSY;
}
#endif /* MY_DEF_HERE */

struct ata_scsi_args {
	struct ata_device	*dev;
	u16			*id;
	struct scsi_cmnd	*cmd;
};

/**
 *	ata_scsi_rbuf_get - Map response buffer.
 *	@cmd: SCSI command containing buffer to be mapped.
 *	@flags: unsigned long variable to store irq enable status
 *	@copy_in: copy in from user buffer
 *
 *	Prepare buffer for simulated SCSI commands.
 *
 *	LOCKING:
 *	spin_lock_irqsave(ata_scsi_rbuf_lock) on success
 *
 *	RETURNS:
 *	Pointer to response buffer.
 */
static void *ata_scsi_rbuf_get(struct scsi_cmnd *cmd, bool copy_in,
			       unsigned long *flags)
{
	spin_lock_irqsave(&ata_scsi_rbuf_lock, *flags);

	memset(ata_scsi_rbuf, 0, ATA_SCSI_RBUF_SIZE);
	if (copy_in)
		sg_copy_to_buffer(scsi_sglist(cmd), scsi_sg_count(cmd),
				  ata_scsi_rbuf, ATA_SCSI_RBUF_SIZE);
	return ata_scsi_rbuf;
}

/**
 *	ata_scsi_rbuf_put - Unmap response buffer.
 *	@cmd: SCSI command containing buffer to be unmapped.
 *	@copy_out: copy out result
 *	@flags: @flags passed to ata_scsi_rbuf_get()
 *
 *	Returns rbuf buffer.  The result is copied to @cmd's buffer if
 *	@copy_back is true.
 *
 *	LOCKING:
 *	Unlocks ata_scsi_rbuf_lock.
 */
static inline void ata_scsi_rbuf_put(struct scsi_cmnd *cmd, bool copy_out,
				     unsigned long *flags)
{
	if (copy_out)
		sg_copy_from_buffer(scsi_sglist(cmd), scsi_sg_count(cmd),
				    ata_scsi_rbuf, ATA_SCSI_RBUF_SIZE);
	spin_unlock_irqrestore(&ata_scsi_rbuf_lock, *flags);
}

/**
 *	ata_scsi_rbuf_fill - wrapper for SCSI command simulators
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@actor: Callback hook for desired SCSI command simulator
 *
 *	Takes care of the hard work of simulating a SCSI command...
 *	Mapping the response buffer, calling the command's handler,
 *	and handling the handler's return value.  This return value
 *	indicates whether the handler wishes the SCSI command to be
 *	completed successfully (0), or not (in which case cmd->result
 *	and sense buffer are assumed to be set).
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void ata_scsi_rbuf_fill(struct ata_scsi_args *args,
		unsigned int (*actor)(struct ata_scsi_args *args, u8 *rbuf))
{
	u8 *rbuf;
	unsigned int rc;
	struct scsi_cmnd *cmd = args->cmd;
	unsigned long flags;

	rbuf = ata_scsi_rbuf_get(cmd, false, &flags);
	rc = actor(args, rbuf);
	ata_scsi_rbuf_put(cmd, rc == 0, &flags);

	if (rc == 0)
		cmd->result = SAM_STAT_GOOD;
}

/**
 *	ata_scsiop_inq_std - Simulate INQUIRY command
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Returns standard device identification data associated
 *	with non-VPD INQUIRY command output.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_inq_std(struct ata_scsi_args *args, u8 *rbuf)
{
	static const u8 versions[] = {
		0x00,
		0x60,	/* SAM-3 (no version claimed) */

		0x03,
		0x20,	/* SBC-2 (no version claimed) */

		0x03,
		0x00	/* SPC-3 (no version claimed) */
	};
	static const u8 versions_zbc[] = {
		0x00,
		0xA0,	/* SAM-5 (no version claimed) */

		0x06,
		0x00,	/* SBC-4 (no version claimed) */

		0x05,
		0xC0,	/* SPC-5 (no version claimed) */

		0x60,
		0x24,   /* ZBC r05 */
	};

	u8 hdr[] = {
		TYPE_DISK,
		0,
		0x5,	/* claim SPC-3 version compatibility */
		2,
		95 - 4,
		0,
		0,
		2
	};

#ifdef MY_ABC_HERE
	unsigned char szIdBuf[ATA_ID_PROD_LEN + 1] = {0x00};
	int idxStr, idxModelStr;
	char bHasSpace = 0;
#endif /* MY_ABC_HERE */
	VPRINTK("ENTER\n");

	/* set scsi removable (RMB) bit per ata bit, or if the
	 * AHCI port says it's external (Hotplug-capable, eSATA).
	 */
	if (ata_id_removable(args->id) ||
	    (args->dev->link->ap->pflags & ATA_PFLAG_EXTERNAL))
		hdr[1] |= (1 << 7);

	if (args->dev->class == ATA_DEV_ZAC) {
		hdr[0] = TYPE_ZBC;
		hdr[2] = 0x7; /* claim SPC-5 version compatibility */
	}

	memcpy(rbuf, hdr, sizeof(hdr));
#ifdef MY_ABC_HERE
	ata_id_c_string(args->id, szIdBuf, ATA_ID_PROD, ATA_ID_PROD_LEN+1);

	for (idxStr = 0; idxStr < ATA_ID_PROD_LEN; idxStr++) {
		if (' ' == szIdBuf[idxStr]) {
			bHasSpace = 1;
			break;
		}

		if (0x00 == szIdBuf[idxStr]) {
			break;
		}
	}

	if (0 == bHasSpace) {
		memcpy(&rbuf[8], "ATA     ", 8);
		ata_id_string(args->id, &rbuf[16], ATA_ID_PROD, 16);
	} else {
		for (idxStr = 0; idxStr < 8; idxStr++) {
			if (' ' == szIdBuf[idxStr]) {
				break;
			}
			rbuf[8 + idxStr] = szIdBuf[idxStr];
		}
		while (' ' == szIdBuf[idxStr]) {
			idxStr++;
		}
		for (idxModelStr = 0; idxModelStr < 16; idxModelStr++) {
			if (' ' == szIdBuf[idxStr]) {
				break;
			}
			rbuf[16 + idxModelStr] = szIdBuf[idxStr];
			idxStr++;
		}
	}
#else /* MY_ABC_HERE */
	memcpy(&rbuf[8], "ATA     ", 8);
	ata_id_string(args->id, &rbuf[16], ATA_ID_PROD, 16);
#endif /* MY_ABC_HERE */

	/* From SAT, use last 2 words from fw rev unless they are spaces */
	ata_id_string(args->id, &rbuf[32], ATA_ID_FW_REV + 2, 4);
	if (strncmp(&rbuf[32], "    ", 4) == 0)
		ata_id_string(args->id, &rbuf[32], ATA_ID_FW_REV, 4);

	if (rbuf[32] == 0 || rbuf[32] == ' ')
		memcpy(&rbuf[32], "n/a ", 4);

	if (ata_id_zoned_cap(args->id) || args->dev->class == ATA_DEV_ZAC)
		memcpy(rbuf + 58, versions_zbc, sizeof(versions_zbc));
	else
		memcpy(rbuf + 58, versions, sizeof(versions));

	return 0;
}

/**
 *	ata_scsiop_inq_00 - Simulate INQUIRY VPD page 0, list of pages
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Returns list of inquiry VPD pages available.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_inq_00(struct ata_scsi_args *args, u8 *rbuf)
{
	int num_pages;
	static const u8 pages[] = {
		0x00,	/* page 0x00, this page */
		0x80,	/* page 0x80, unit serial no page */
		0x83,	/* page 0x83, device ident page */
		0x89,	/* page 0x89, ata info page */
		0xb0,	/* page 0xb0, block limits page */
		0xb1,	/* page 0xb1, block device characteristics page */
		0xb2,	/* page 0xb2, thin provisioning page */
		0xb6,	/* page 0xb6, zoned block device characteristics */
	};

	num_pages = sizeof(pages);
	if (!(args->dev->flags & ATA_DFLAG_ZAC))
		num_pages--;
	rbuf[3] = num_pages;	/* number of supported VPD pages */
	memcpy(rbuf + 4, pages, num_pages);
	return 0;
}

/**
 *	ata_scsiop_inq_80 - Simulate INQUIRY VPD page 80, device serial number
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Returns ATA device serial number.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *rbuf)
{
	static const u8 hdr[] = {
		0,
		0x80,			/* this page code */
		0,
		ATA_ID_SERNO_LEN,	/* page len */
	};

	memcpy(rbuf, hdr, sizeof(hdr));
	ata_id_string(args->id, (unsigned char *) &rbuf[4],
		      ATA_ID_SERNO, ATA_ID_SERNO_LEN);
	return 0;
}

/**
 *	ata_scsiop_inq_83 - Simulate INQUIRY VPD page 83, device identity
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Yields two logical unit device identification designators:
 *	 - vendor specific ASCII containing the ATA serial number
 *	 - SAT defined "t10 vendor id based" containing ASCII vendor
 *	   name ("ATA     "), model and serial numbers.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf)
{
	const int sat_model_serial_desc_len = 68;
	int num;

	rbuf[1] = 0x83;			/* this page code */
	num = 4;

	/* piv=0, assoc=lu, code_set=ACSII, designator=vendor */
	rbuf[num + 0] = 2;
	rbuf[num + 3] = ATA_ID_SERNO_LEN;
	num += 4;
	ata_id_string(args->id, (unsigned char *) rbuf + num,
		      ATA_ID_SERNO, ATA_ID_SERNO_LEN);
	num += ATA_ID_SERNO_LEN;

	/* SAT defined lu model and serial numbers descriptor */
	/* piv=0, assoc=lu, code_set=ACSII, designator=t10 vendor id */
	rbuf[num + 0] = 2;
	rbuf[num + 1] = 1;
	rbuf[num + 3] = sat_model_serial_desc_len;
	num += 4;
	memcpy(rbuf + num, "ATA     ", 8);
	num += 8;
	ata_id_string(args->id, (unsigned char *) rbuf + num, ATA_ID_PROD,
		      ATA_ID_PROD_LEN);
	num += ATA_ID_PROD_LEN;
	ata_id_string(args->id, (unsigned char *) rbuf + num, ATA_ID_SERNO,
		      ATA_ID_SERNO_LEN);
	num += ATA_ID_SERNO_LEN;

	if (ata_id_has_wwn(args->id)) {
		/* SAT defined lu world wide name */
		/* piv=0, assoc=lu, code_set=binary, designator=NAA */
		rbuf[num + 0] = 1;
		rbuf[num + 1] = 3;
		rbuf[num + 3] = ATA_ID_WWN_LEN;
		num += 4;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_WWN, ATA_ID_WWN_LEN);
		num += ATA_ID_WWN_LEN;
	}
	rbuf[3] = num - 4;    /* page len (assume less than 256 bytes) */
	return 0;
}

/**
 *	ata_scsiop_inq_89 - Simulate INQUIRY VPD page 89, ATA info
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Yields SAT-specified ATA VPD page.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_inq_89(struct ata_scsi_args *args, u8 *rbuf)
{
	rbuf[1] = 0x89;			/* our page code */
	rbuf[2] = (0x238 >> 8);		/* page size fixed at 238h */
	rbuf[3] = (0x238 & 0xff);

	memcpy(&rbuf[8], "linux   ", 8);
	memcpy(&rbuf[16], "libata          ", 16);
	memcpy(&rbuf[32], DRV_VERSION, 4);

	rbuf[36] = 0x34;		/* force D2H Reg FIS (34h) */
	rbuf[37] = (1 << 7);		/* bit 7 indicates Command FIS */
					/* TODO: PMP? */

	/* we don't store the ATA device signature, so we fake it */
	rbuf[38] = ATA_DRDY;		/* really, this is Status reg */
	rbuf[40] = 0x1;
	rbuf[48] = 0x1;

	rbuf[56] = ATA_CMD_ID_ATA;

	memcpy(&rbuf[60], &args->id[0], 512);
	return 0;
}

static unsigned int ata_scsiop_inq_b0(struct ata_scsi_args *args, u8 *rbuf)
{
	struct ata_device *dev = args->dev;
	u16 min_io_sectors;

	rbuf[1] = 0xb0;
	rbuf[3] = 0x3c;		/* required VPD size with unmap support */

	/*
	 * Optimal transfer length granularity.
	 *
	 * This is always one physical block, but for disks with a smaller
	 * logical than physical sector size we need to figure out what the
	 * latter is.
	 */
	min_io_sectors = 1 << ata_id_log2_per_physical_sector(args->id);
	put_unaligned_be16(min_io_sectors, &rbuf[6]);

	/*
	 * Optimal unmap granularity.
	 *
	 * The ATA spec doesn't even know about a granularity or alignment
	 * for the TRIM command.  We can leave away most of the unmap related
	 * VPD page entries, but we have specifify a granularity to signal
	 * that we support some form of unmap - in thise case via WRITE SAME
	 * with the unmap bit set.
	 */
	if (ata_id_has_trim(args->id)) {
		u64 max_blocks = 65535 * ATA_MAX_TRIM_RNUM;

		if (dev->horkage & ATA_HORKAGE_MAX_TRIM_128M)
			max_blocks = 128 << (20 - SECTOR_SHIFT);

		put_unaligned_be64(max_blocks, &rbuf[36]);
		put_unaligned_be32(1, &rbuf[28]);
	}

	return 0;
}

static unsigned int ata_scsiop_inq_b1(struct ata_scsi_args *args, u8 *rbuf)
{
	int form_factor = ata_id_form_factor(args->id);
	int media_rotation_rate = ata_id_rotation_rate(args->id);
	u8 zoned = ata_id_zoned_cap(args->id);

	rbuf[1] = 0xb1;
	rbuf[3] = 0x3c;
	rbuf[4] = media_rotation_rate >> 8;
	rbuf[5] = media_rotation_rate;
	rbuf[7] = form_factor;
	if (zoned)
		rbuf[8] = (zoned << 4);

	return 0;
}

static unsigned int ata_scsiop_inq_b2(struct ata_scsi_args *args, u8 *rbuf)
{
	/* SCSI Thin Provisioning VPD page: SBC-3 rev 22 or later */
	rbuf[1] = 0xb2;
	rbuf[3] = 0x4;
	rbuf[5] = 1 << 6;	/* TPWS */

	return 0;
}

static unsigned int ata_scsiop_inq_b6(struct ata_scsi_args *args, u8 *rbuf)
{
	/*
	 * zbc-r05 SCSI Zoned Block device characteristics VPD page
	 */
	rbuf[1] = 0xb6;
	rbuf[3] = 0x3C;

	/*
	 * URSWRZ bit is only meaningful for host-managed ZAC drives
	 */
	if (args->dev->zac_zoned_cap & 1)
		rbuf[4] |= 1;
	put_unaligned_be32(args->dev->zac_zones_optimal_open, &rbuf[8]);
	put_unaligned_be32(args->dev->zac_zones_optimal_nonseq, &rbuf[12]);
	put_unaligned_be32(args->dev->zac_zones_max_open, &rbuf[16]);

	return 0;
}

/**
 *	modecpy - Prepare response for MODE SENSE
 *	@dest: output buffer
 *	@src: data being copied
 *	@n: length of mode page
 *	@changeable: whether changeable parameters are requested
 *
 *	Generate a generic MODE SENSE page for either current or changeable
 *	parameters.
 *
 *	LOCKING:
 *	None.
 */
static void modecpy(u8 *dest, const u8 *src, int n, bool changeable)
{
	if (changeable) {
		memcpy(dest, src, 2);
		memset(dest + 2, 0, n - 2);
	} else {
		memcpy(dest, src, n);
	}
}

/**
 *	ata_msense_caching - Simulate MODE SENSE caching info page
 *	@id: device IDENTIFY data
 *	@buf: output buffer
 *	@changeable: whether changeable parameters are requested
 *
 *	Generate a caching info page, which conditionally indicates
 *	write caching to the SCSI layer, depending on device
 *	capabilities.
 *
 *	LOCKING:
 *	None.
 */
static unsigned int ata_msense_caching(u16 *id, u8 *buf, bool changeable)
{
	modecpy(buf, def_cache_mpage, sizeof(def_cache_mpage), changeable);
	if (changeable) {
		buf[2] |= (1 << 2);	/* ata_mselect_caching() */
	} else {
		buf[2] |= (ata_id_wcache_enabled(id) << 2);	/* write cache enable */
		buf[12] |= (!ata_id_rahead_enabled(id) << 5);	/* disable read ahead */
	}
	return sizeof(def_cache_mpage);
}

/**
 *	ata_msense_control - Simulate MODE SENSE control mode page
 *	@dev: ATA device of interest
 *	@buf: output buffer
 *	@changeable: whether changeable parameters are requested
 *
 *	Generate a generic MODE SENSE control mode page.
 *
 *	LOCKING:
 *	None.
 */
static unsigned int ata_msense_control(struct ata_device *dev, u8 *buf,
					bool changeable)
{
	modecpy(buf, def_control_mpage, sizeof(def_control_mpage), changeable);
	if (changeable) {
		buf[2] |= (1 << 2);	/* ata_mselect_control() */
	} else {
		bool d_sense = (dev->flags & ATA_DFLAG_D_SENSE);

		buf[2] |= (d_sense << 2);	/* descriptor format sense data */
	}
	return sizeof(def_control_mpage);
}

/**
 *	ata_msense_rw_recovery - Simulate MODE SENSE r/w error recovery page
 *	@buf: output buffer
 *	@changeable: whether changeable parameters are requested
 *
 *	Generate a generic MODE SENSE r/w error recovery page.
 *
 *	LOCKING:
 *	None.
 */
static unsigned int ata_msense_rw_recovery(u8 *buf, bool changeable)
{
	modecpy(buf, def_rw_recovery_mpage, sizeof(def_rw_recovery_mpage),
		changeable);
	return sizeof(def_rw_recovery_mpage);
}

/*
 * We can turn this into a real blacklist if it's needed, for now just
 * blacklist any Maxtor BANC1G10 revision firmware
 */
static int ata_dev_supports_fua(u16 *id)
{
	unsigned char model[ATA_ID_PROD_LEN + 1], fw[ATA_ID_FW_REV_LEN + 1];

	if (!libata_fua)
		return 0;
	if (!ata_id_has_fua(id))
		return 0;

	ata_id_c_string(id, model, ATA_ID_PROD, sizeof(model));
	ata_id_c_string(id, fw, ATA_ID_FW_REV, sizeof(fw));

	if (strcmp(model, "Maxtor"))
		return 1;
	if (strcmp(fw, "BANC1G10"))
		return 1;

	return 0; /* blacklisted */
}

/**
 *	ata_scsiop_mode_sense - Simulate MODE SENSE 6, 10 commands
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Simulate MODE SENSE commands. Assume this is invoked for direct
 *	access devices (e.g. disks) only. There should be no block
 *	descriptor for other device types.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *rbuf)
{
	struct ata_device *dev = args->dev;
	u8 *scsicmd = args->cmd->cmnd, *p = rbuf;
	static const u8 sat_blk_desc[] = {
		0, 0, 0, 0,	/* number of blocks: sat unspecified */
		0,
		0, 0x2, 0x0	/* block length: 512 bytes */
	};
	u8 pg, spg;
	unsigned int ebd, page_control, six_byte;
	u8 dpofua, bp = 0xff;
	u16 fp;

	VPRINTK("ENTER\n");

	six_byte = (scsicmd[0] == MODE_SENSE);
	ebd = !(scsicmd[1] & 0x8);      /* dbd bit inverted == edb */
	/*
	 * LLBA bit in msense(10) ignored (compliant)
	 */

	page_control = scsicmd[2] >> 6;
	switch (page_control) {
	case 0: /* current */
	case 1: /* changeable */
	case 2: /* defaults */
		break;  /* supported */
	case 3: /* saved */
		goto saving_not_supp;
	default:
		fp = 2;
		bp = 6;
		goto invalid_fld;
	}

	if (six_byte)
		p += 4 + (ebd ? 8 : 0);
	else
		p += 8 + (ebd ? 8 : 0);

	pg = scsicmd[2] & 0x3f;
	spg = scsicmd[3];
	/*
	 * No mode subpages supported (yet) but asking for _all_
	 * subpages may be valid
	 */
	if (spg && (spg != ALL_SUB_MPAGES)) {
		fp = 3;
		goto invalid_fld;
	}

	switch(pg) {
	case RW_RECOVERY_MPAGE:
		p += ata_msense_rw_recovery(p, page_control == 1);
		break;

	case CACHE_MPAGE:
		p += ata_msense_caching(args->id, p, page_control == 1);
		break;

	case CONTROL_MPAGE:
		p += ata_msense_control(args->dev, p, page_control == 1);
		break;

	case ALL_MPAGES:
		p += ata_msense_rw_recovery(p, page_control == 1);
		p += ata_msense_caching(args->id, p, page_control == 1);
		p += ata_msense_control(args->dev, p, page_control == 1);
		break;

	default:		/* invalid page code */
		fp = 2;
		goto invalid_fld;
	}

	dpofua = 0;
	if (ata_dev_supports_fua(args->id) && (dev->flags & ATA_DFLAG_LBA48) &&
	    (!(dev->flags & ATA_DFLAG_PIO) || dev->multi_count))
		dpofua = 1 << 4;

	if (six_byte) {
		rbuf[0] = p - rbuf - 1;
		rbuf[2] |= dpofua;
		if (ebd) {
			rbuf[3] = sizeof(sat_blk_desc);
			memcpy(rbuf + 4, sat_blk_desc, sizeof(sat_blk_desc));
		}
	} else {
		unsigned int output_len = p - rbuf - 2;

		rbuf[0] = output_len >> 8;
		rbuf[1] = output_len;
		rbuf[3] |= dpofua;
		if (ebd) {
			rbuf[7] = sizeof(sat_blk_desc);
			memcpy(rbuf + 8, sat_blk_desc, sizeof(sat_blk_desc));
		}
	}
	return 0;

invalid_fld:
	ata_scsi_set_invalid_field(dev, args->cmd, fp, bp);
	return 1;

saving_not_supp:
	ata_scsi_set_sense(dev, args->cmd, ILLEGAL_REQUEST, 0x39, 0x0);
	 /* "Saving parameters not supported" */
	return 1;
}

/**
 *	ata_scsiop_read_cap - Simulate READ CAPACITY[ 16] commands
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Simulate READ CAPACITY commands.
 *
 *	LOCKING:
 *	None.
 */
static unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *rbuf)
{
	struct ata_device *dev = args->dev;
	u64 last_lba = dev->n_sectors - 1; /* LBA of the last block */
	u32 sector_size; /* physical sector size in bytes */
	u8 log2_per_phys;
	u16 lowest_aligned;

	sector_size = ata_id_logical_sector_size(dev->id);
	log2_per_phys = ata_id_log2_per_physical_sector(dev->id);
	lowest_aligned = ata_id_logical_sector_offset(dev->id, log2_per_phys);

	VPRINTK("ENTER\n");

	if (args->cmd->cmnd[0] == READ_CAPACITY) {
		if (last_lba >= 0xffffffffULL)
			last_lba = 0xffffffff;

		/* sector count, 32-bit */
		rbuf[0] = last_lba >> (8 * 3);
		rbuf[1] = last_lba >> (8 * 2);
		rbuf[2] = last_lba >> (8 * 1);
		rbuf[3] = last_lba;

		/* sector size */
		rbuf[4] = sector_size >> (8 * 3);
		rbuf[5] = sector_size >> (8 * 2);
		rbuf[6] = sector_size >> (8 * 1);
		rbuf[7] = sector_size;
	} else {
		/* sector count, 64-bit */
		rbuf[0] = last_lba >> (8 * 7);
		rbuf[1] = last_lba >> (8 * 6);
		rbuf[2] = last_lba >> (8 * 5);
		rbuf[3] = last_lba >> (8 * 4);
		rbuf[4] = last_lba >> (8 * 3);
		rbuf[5] = last_lba >> (8 * 2);
		rbuf[6] = last_lba >> (8 * 1);
		rbuf[7] = last_lba;

		/* sector size */
		rbuf[ 8] = sector_size >> (8 * 3);
		rbuf[ 9] = sector_size >> (8 * 2);
		rbuf[10] = sector_size >> (8 * 1);
		rbuf[11] = sector_size;

		rbuf[12] = 0;
		rbuf[13] = log2_per_phys;
		rbuf[14] = (lowest_aligned >> 8) & 0x3f;
		rbuf[15] = lowest_aligned;

		if (ata_id_has_trim(args->id) &&
		    !(dev->horkage & ATA_HORKAGE_NOTRIM)) {
			rbuf[14] |= 0x80; /* LBPME */

			if (ata_id_has_zero_after_trim(args->id) &&
#ifdef MY_ABC_HERE
				1 ) {
#else /* MY_ABC_HERE */
			    dev->horkage & ATA_HORKAGE_ZERO_AFTER_TRIM) {
#endif /* MY_ABC_HERE */
				ata_dev_info(dev, "Enabling discard_zeroes_data\n");
				rbuf[14] |= 0x40; /* LBPRZ */
			}
		}
		if (ata_id_zoned_cap(args->id) ||
		    args->dev->class == ATA_DEV_ZAC)
			rbuf[12] = (1 << 4); /* RC_BASIS */
	}
	return 0;
}

/**
 *	ata_scsiop_report_luns - Simulate REPORT LUNS command
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Simulate REPORT LUNS command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_report_luns(struct ata_scsi_args *args, u8 *rbuf)
{
	VPRINTK("ENTER\n");
	rbuf[3] = 8;	/* just one lun, LUN 0, size 8 bytes */

	return 0;
}

static void atapi_sense_complete(struct ata_queued_cmd *qc)
{
	if (qc->err_mask && ((qc->err_mask & AC_ERR_DEV) == 0)) {
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_passthru_sense(qc);
	}

	ata_qc_done(qc);
}

/* is it pointless to prefer PIO for "safety reasons"? */
static inline int ata_pio_use_silly(struct ata_port *ap)
{
	return (ap->flags & ATA_FLAG_PIO_DMA);
}

static void atapi_request_sense(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;

	DPRINTK("ATAPI request sense\n");

	memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);

#ifdef CONFIG_ATA_SFF
	if (ap->ops->sff_tf_read)
		ap->ops->sff_tf_read(ap, &qc->tf);
#endif

	/* fill these in, for the case where they are -not- overwritten */
	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = qc->tf.feature >> 4;

	ata_qc_reinit(qc);

	/* setup sg table and init transfer direction */
	sg_init_one(&qc->sgent, cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE);
	ata_sg_init(qc, &qc->sgent, 1);
	qc->dma_dir = DMA_FROM_DEVICE;

	memset(&qc->cdb, 0, qc->dev->cdb_len);
	qc->cdb[0] = REQUEST_SENSE;
	qc->cdb[4] = SCSI_SENSE_BUFFERSIZE;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	qc->tf.command = ATA_CMD_PACKET;

	if (ata_pio_use_silly(ap)) {
		qc->tf.protocol = ATAPI_PROT_DMA;
		qc->tf.feature |= ATAPI_PKT_DMA;
	} else {
		qc->tf.protocol = ATAPI_PROT_PIO;
		qc->tf.lbam = SCSI_SENSE_BUFFERSIZE;
		qc->tf.lbah = 0;
	}
	qc->nbytes = SCSI_SENSE_BUFFERSIZE;

	qc->complete_fn = atapi_sense_complete;

	ata_qc_issue(qc);

	DPRINTK("EXIT\n");
}

/*
 * ATAPI devices typically report zero for their SCSI version, and sometimes
 * deviate from the spec WRT response data format.  If SCSI version is
 * reported as zero like normal, then we make the following fixups:
 *   1) Fake MMC-5 version, to indicate to the Linux scsi midlayer this is a
 *	modern device.
 *   2) Ensure response data format / ATAPI information are always correct.
 */
static void atapi_fixup_inquiry(struct scsi_cmnd *cmd)
{
	u8 buf[4];

	sg_copy_to_buffer(scsi_sglist(cmd), scsi_sg_count(cmd), buf, 4);
	if (buf[2] == 0) {
		buf[2] = 0x5;
		buf[3] = 0x32;
	}
	sg_copy_from_buffer(scsi_sglist(cmd), scsi_sg_count(cmd), buf, 4);
}

static void atapi_qc_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	unsigned int err_mask = qc->err_mask;

	VPRINTK("ENTER, err_mask 0x%X\n", err_mask);

	/* handle completion from new EH */
	if (unlikely(qc->ap->ops->error_handler &&
		     (err_mask || qc->flags & ATA_QCFLAG_SENSE_VALID))) {

		if (!(qc->flags & ATA_QCFLAG_SENSE_VALID)) {
			/* FIXME: not quite right; we don't want the
			 * translation of taskfile registers into a
			 * sense descriptors, since that's only
			 * correct for ATA, not ATAPI
			 */
			ata_gen_passthru_sense(qc);
		}

		/* SCSI EH automatically locks door if sdev->locked is
		 * set.  Sometimes door lock request continues to
		 * fail, for example, when no media is present.  This
		 * creates a loop - SCSI EH issues door lock which
		 * fails and gets invoked again to acquire sense data
		 * for the failed command.
		 *
		 * If door lock fails, always clear sdev->locked to
		 * avoid this infinite loop.
		 *
		 * This may happen before SCSI scan is complete.  Make
		 * sure qc->dev->sdev isn't NULL before dereferencing.
		 */
		if (qc->cdb[0] == ALLOW_MEDIUM_REMOVAL && qc->dev->sdev)
			qc->dev->sdev->locked = 0;

		qc->scsicmd->result = SAM_STAT_CHECK_CONDITION;
		ata_qc_done(qc);
		return;
	}

	/* successful completion or old EH failure path */
	if (unlikely(err_mask & AC_ERR_DEV)) {
		cmd->result = SAM_STAT_CHECK_CONDITION;
		atapi_request_sense(qc);
		return;
	} else if (unlikely(err_mask)) {
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_passthru_sense(qc);
	} else {
		if (cmd->cmnd[0] == INQUIRY && (cmd->cmnd[1] & 0x03) == 0)
			atapi_fixup_inquiry(cmd);
		cmd->result = SAM_STAT_GOOD;
	}

	ata_qc_done(qc);
}
/**
 *	atapi_xlat - Initialize PACKET taskfile
 *	@qc: command structure to be initialized
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */
static unsigned int atapi_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	int nodata = (scmd->sc_data_direction == DMA_NONE);
	int using_pio = !nodata && (dev->flags & ATA_DFLAG_PIO);
	unsigned int nbytes;

	memset(qc->cdb, 0, dev->cdb_len);
	memcpy(qc->cdb, scmd->cmnd, scmd->cmd_len);

	qc->complete_fn = atapi_qc_complete;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (scmd->sc_data_direction == DMA_TO_DEVICE) {
		qc->tf.flags |= ATA_TFLAG_WRITE;
		DPRINTK("direction: write\n");
	}

	qc->tf.command = ATA_CMD_PACKET;
	ata_qc_set_pc_nbytes(qc);

	/* check whether ATAPI DMA is safe */
	if (!nodata && !using_pio && atapi_check_dma(qc))
		using_pio = 1;

	/* Some controller variants snoop this value for Packet
	 * transfers to do state machine and FIFO management.  Thus we
	 * want to set it properly, and for DMA where it is
	 * effectively meaningless.
	 */
	nbytes = min(ata_qc_raw_nbytes(qc), (unsigned int)63 * 1024);

	/* Most ATAPI devices which honor transfer chunk size don't
	 * behave according to the spec when odd chunk size which
	 * matches the transfer length is specified.  If the number of
	 * bytes to transfer is 2n+1.  According to the spec, what
	 * should happen is to indicate that 2n+1 is going to be
	 * transferred and transfer 2n+2 bytes where the last byte is
	 * padding.
	 *
	 * In practice, this doesn't happen.  ATAPI devices first
	 * indicate and transfer 2n bytes and then indicate and
	 * transfer 2 bytes where the last byte is padding.
	 *
	 * This inconsistency confuses several controllers which
	 * perform PIO using DMA such as Intel AHCIs and sil3124/32.
	 * These controllers use actual number of transferred bytes to
	 * update DMA pointer and transfer of 4n+2 bytes make those
	 * controller push DMA pointer by 4n+4 bytes because SATA data
	 * FISes are aligned to 4 bytes.  This causes data corruption
	 * and buffer overrun.
	 *
	 * Always setting nbytes to even number solves this problem
	 * because then ATAPI devices don't have to split data at 2n
	 * boundaries.
	 */
	if (nbytes & 0x1)
		nbytes++;

	qc->tf.lbam = (nbytes & 0xFF);
	qc->tf.lbah = (nbytes >> 8);

	if (nodata)
		qc->tf.protocol = ATAPI_PROT_NODATA;
	else if (using_pio)
		qc->tf.protocol = ATAPI_PROT_PIO;
	else {
		/* DMA data xfer */
		qc->tf.protocol = ATAPI_PROT_DMA;
		qc->tf.feature |= ATAPI_PKT_DMA;

		if ((dev->flags & ATA_DFLAG_DMADIR) &&
		    (scmd->sc_data_direction != DMA_TO_DEVICE))
			/* some SATA bridges need us to indicate data xfer direction */
			qc->tf.feature |= ATAPI_DMADIR;
	}


	/* FIXME: We need to translate 0x05 READ_BLOCK_LIMITS to a MODE_SENSE
	   as ATAPI tape drives don't get this right otherwise */
	return 0;
}

static struct ata_device *ata_find_dev(struct ata_port *ap, int devno)
{
	if (!sata_pmp_attached(ap)) {
		if (likely(devno >= 0 &&
			   devno < ata_link_max_devices(&ap->link)))
			return &ap->link.device[devno];
	} else {
		if (likely(devno >= 0 &&
			   devno < ap->nr_pmp_links))
			return &ap->pmp_link[devno].device[0];
#ifdef MY_ABC_HERE
		else if (devno == SYNO_PM_VIRTUAL_SCSI_CHANNEL && syno_is_synology_pm(ap)) {
			return &ap->link.device[0];
		}
#endif /* MY_ABC_HERE */
	}

	return NULL;
}

static struct ata_device *__ata_scsi_find_dev(struct ata_port *ap,
					      const struct scsi_device *scsidev)
{
	int devno;

	/* skip commands not addressed to targets we simulate */
	if (!sata_pmp_attached(ap)) {
		if (unlikely(scsidev->channel || scsidev->lun))
			return NULL;
		devno = scsidev->id;
	} else {
		if (unlikely(scsidev->id || scsidev->lun))
			return NULL;
		devno = scsidev->channel;
	}

	return ata_find_dev(ap, devno);
}

/**
 *	ata_scsi_find_dev - lookup ata_device from scsi_cmnd
 *	@ap: ATA port to which the device is attached
 *	@scsidev: SCSI device from which we derive the ATA device
 *
 *	Given various information provided in struct scsi_cmnd,
 *	map that onto an ATA bus, and using that mapping
 *	determine which ata_device is associated with the
 *	SCSI command to be sent.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Associated ATA device, or %NULL if not found.
 */
struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, const struct scsi_device *scsidev)
{
	struct ata_device *dev = __ata_scsi_find_dev(ap, scsidev);

	if (unlikely(!dev || !ata_dev_enabled(dev)))
		return NULL;

	return dev;
}

/*
 *	ata_scsi_map_proto - Map pass-thru protocol value to taskfile value.
 *	@byte1: Byte 1 from pass-thru CDB.
 *
 *	RETURNS:
 *	ATA_PROT_UNKNOWN if mapping failed/unimplemented, protocol otherwise.
 */
static u8
ata_scsi_map_proto(u8 byte1)
{
	switch((byte1 & 0x1e) >> 1) {
	case 3:		/* Non-data */
		return ATA_PROT_NODATA;

	case 6:		/* DMA */
	case 10:	/* UDMA Data-in */
	case 11:	/* UDMA Data-Out */
		return ATA_PROT_DMA;

	case 4:		/* PIO Data-in */
	case 5:		/* PIO Data-out */
		return ATA_PROT_PIO;

	case 12:	/* FPDMA */
		return ATA_PROT_NCQ;

	case 0:		/* Hard Reset */
	case 1:		/* SRST */
	case 8:		/* Device Diagnostic */
	case 9:		/* Device Reset */
	case 7:		/* DMA Queued */
	case 15:	/* Return Response Info */
	default:	/* Reserved */
		break;
	}

	return ATA_PROT_UNKNOWN;
}

/**
 *	ata_scsi_pass_thru - convert ATA pass-thru CDB to taskfile
 *	@qc: command structure to be initialized
 *
 *	Handles either 12, 16, or 32-byte versions of the CDB.
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */
static unsigned int ata_scsi_pass_thru(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &(qc->tf);
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	const u8 *cdb = scmd->cmnd;
	u16 fp;
	u16 cdb_offset = 0;

	/* 7Fh variable length cmd means a ata pass-thru(32) */
	if (cdb[0] == VARIABLE_LENGTH_CMD)
		cdb_offset = 9;

	tf->protocol = ata_scsi_map_proto(cdb[1 + cdb_offset]);
	if (tf->protocol == ATA_PROT_UNKNOWN) {
		fp = 1;
		goto invalid_fld;
	}

	if (ata_is_ncq(tf->protocol) && (cdb[2 + cdb_offset] & 0x3) == 0)
		tf->protocol = ATA_PROT_NCQ_NODATA;

	/* enable LBA */
	tf->flags |= ATA_TFLAG_LBA;

	/*
	 * 12 and 16 byte CDBs use different offsets to
	 * provide the various register values.
	 */
	if (cdb[0] == ATA_16) {
		/*
		 * 16-byte CDB - may contain extended commands.
		 *
		 * If that is the case, copy the upper byte register values.
		 */
		if (cdb[1] & 0x01) {
			tf->hob_feature = cdb[3];
			tf->hob_nsect = cdb[5];
			tf->hob_lbal = cdb[7];
			tf->hob_lbam = cdb[9];
			tf->hob_lbah = cdb[11];
			tf->flags |= ATA_TFLAG_LBA48;
		} else
			tf->flags &= ~ATA_TFLAG_LBA48;

		/*
		 * Always copy low byte, device and command registers.
		 */
		tf->feature = cdb[4];
		tf->nsect = cdb[6];
		tf->lbal = cdb[8];
		tf->lbam = cdb[10];
		tf->lbah = cdb[12];
		tf->device = cdb[13];
		tf->command = cdb[14];
	} else if (cdb[0] == ATA_12) {
		/*
		 * 12-byte CDB - incapable of extended commands.
		 */
		tf->flags &= ~ATA_TFLAG_LBA48;

		tf->feature = cdb[3];
		tf->nsect = cdb[4];
		tf->lbal = cdb[5];
		tf->lbam = cdb[6];
		tf->lbah = cdb[7];
		tf->device = cdb[8];
		tf->command = cdb[9];
	} else {
		/*
		 * 32-byte CDB - may contain extended command fields.
		 *
		 * If that is the case, copy the upper byte register values.
		 */
		if (cdb[10] & 0x01) {
			tf->hob_feature = cdb[20];
			tf->hob_nsect = cdb[22];
			tf->hob_lbal = cdb[16];
			tf->hob_lbam = cdb[15];
			tf->hob_lbah = cdb[14];
			tf->flags |= ATA_TFLAG_LBA48;
		} else
			tf->flags &= ~ATA_TFLAG_LBA48;

		tf->feature = cdb[21];
		tf->nsect = cdb[23];
		tf->lbal = cdb[19];
		tf->lbam = cdb[18];
		tf->lbah = cdb[17];
		tf->device = cdb[24];
		tf->command = cdb[25];
		tf->auxiliary = get_unaligned_be32(&cdb[28]);
	}

	/* For NCQ commands copy the tag value */
	if (ata_is_ncq(tf->protocol))
		tf->nsect = qc->hw_tag << 3;

	/* enforce correct master/slave bit */
	tf->device = dev->devno ?
		tf->device | ATA_DEV1 : tf->device & ~ATA_DEV1;

	switch (tf->command) {
	/* READ/WRITE LONG use a non-standard sect_size */
	case ATA_CMD_READ_LONG:
	case ATA_CMD_READ_LONG_ONCE:
	case ATA_CMD_WRITE_LONG:
	case ATA_CMD_WRITE_LONG_ONCE:
		if (tf->protocol != ATA_PROT_PIO || tf->nsect != 1) {
			fp = 1;
			goto invalid_fld;
		}
		qc->sect_size = scsi_bufflen(scmd);
		break;

	/* commands using reported Logical Block size (e.g. 512 or 4K) */
	case ATA_CMD_CFA_WRITE_NE:
	case ATA_CMD_CFA_TRANS_SECT:
	case ATA_CMD_CFA_WRITE_MULT_NE:
	/* XXX: case ATA_CMD_CFA_WRITE_SECTORS_WITHOUT_ERASE: */
	case ATA_CMD_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_READ_QUEUED:
	/* XXX: case ATA_CMD_READ_QUEUED_EXT: */
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_READ_MULTI:
	case ATA_CMD_READ_MULTI_EXT:
	case ATA_CMD_PIO_READ:
	case ATA_CMD_PIO_READ_EXT:
	case ATA_CMD_READ_STREAM_DMA_EXT:
	case ATA_CMD_READ_STREAM_EXT:
	case ATA_CMD_VERIFY:
	case ATA_CMD_VERIFY_EXT:
	case ATA_CMD_WRITE:
	case ATA_CMD_WRITE_EXT:
	case ATA_CMD_WRITE_FUA_EXT:
	case ATA_CMD_WRITE_QUEUED:
	case ATA_CMD_WRITE_QUEUED_FUA_EXT:
	case ATA_CMD_FPDMA_WRITE:
	case ATA_CMD_WRITE_MULTI:
	case ATA_CMD_WRITE_MULTI_EXT:
	case ATA_CMD_WRITE_MULTI_FUA_EXT:
	case ATA_CMD_PIO_WRITE:
	case ATA_CMD_PIO_WRITE_EXT:
	case ATA_CMD_WRITE_STREAM_DMA_EXT:
	case ATA_CMD_WRITE_STREAM_EXT:
		qc->sect_size = scmd->device->sector_size;
		break;

	/* Everything else uses 512 byte "sectors" */
	default:
		qc->sect_size = ATA_SECT_SIZE;
	}

	/*
	 * Set flags so that all registers will be written, pass on
	 * write indication (used for PIO/DMA setup), result TF is
	 * copied back and we don't whine too much about its failure.
	 */
	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		tf->flags |= ATA_TFLAG_WRITE;

	qc->flags |= ATA_QCFLAG_RESULT_TF | ATA_QCFLAG_QUIET;

	/*
	 * Set transfer length.
	 *
	 * TODO: find out if we need to do more here to
	 *       cover scatter/gather case.
	 */
	ata_qc_set_pc_nbytes(qc);

	/* We may not issue DMA commands if no DMA mode is set */
	if (tf->protocol == ATA_PROT_DMA && dev->dma_mode == 0) {
		fp = 1;
		goto invalid_fld;
	}

	/* We may not issue NCQ commands to devices not supporting NCQ */
	if (ata_is_ncq(tf->protocol) && !ata_ncq_enabled(dev)) {
		fp = 1;
		goto invalid_fld;
	}

	/* sanity check for pio multi commands */
	if ((cdb[1] & 0xe0) && !is_multi_taskfile(tf)) {
		fp = 1;
		goto invalid_fld;
	}

	if (is_multi_taskfile(tf)) {
		unsigned int multi_count = 1 << (cdb[1] >> 5);

		/* compare the passed through multi_count
		 * with the cached multi_count of libata
		 */
		if (multi_count != dev->multi_count)
			ata_dev_warn(dev, "invalid multi_count %u ignored\n",
				     multi_count);
	}

	/*
	 * Filter SET_FEATURES - XFER MODE command -- otherwise,
	 * SET_FEATURES - XFER MODE must be preceded/succeeded
	 * by an update to hardware-specific registers for each
	 * controller (i.e. the reason for ->set_piomode(),
	 * ->set_dmamode(), and ->post_set_mode() hooks).
	 */
	if (tf->command == ATA_CMD_SET_FEATURES &&
	    tf->feature == SETFEATURES_XFER) {
		fp = (cdb[0] == ATA_16) ? 4 : 3;
		goto invalid_fld;
	}

#ifdef MY_ABC_HERE
	if (ATA_CMD_SET_FEATURES == tf->command &&
	    SETFEATURES_WC_ON == tf->feature &&
		(dev->flags & ATA_DFLAG_NO_WCACHE) &&
		(dev->horkage & ATA_HORKAGE_NOWCACHE)) {
		goto skip_cmd;
	}

	if (ATA_CMD_SET_FEATURES == tf->command) {
		if (SETFEATURES_WC_OFF == tf->feature) {
			dev->flags |= ATA_DFLAG_NO_WCACHE;
		} else if (SETFEATURES_WC_ON == tf->feature) {
			dev->flags &= ~ATA_DFLAG_NO_WCACHE;
		}
	}
#endif /* MY_ABC_HERE */

	/*
	 * Filter TPM commands by default. These provide an
	 * essentially uncontrolled encrypted "back door" between
	 * applications and the disk. Set libata.allow_tpm=1 if you
	 * have a real reason for wanting to use them. This ensures
	 * that installed software cannot easily mess stuff up without
	 * user intent. DVR type users will probably ship with this enabled
	 * for movie content management.
	 *
	 * Note that for ATA8 we can issue a DCS change and DCS freeze lock
	 * for this and should do in future but that it is not sufficient as
	 * DCS is an optional feature set. Thus we also do the software filter
	 * so that we comply with the TC consortium stated goal that the user
	 * can turn off TC features of their system.
	 */
	if (tf->command >= 0x5C && tf->command <= 0x5F && !libata_allow_tpm) {
		fp = (cdb[0] == ATA_16) ? 14 : 9;
		goto invalid_fld;
	}

	return 0;

 invalid_fld:
	ata_scsi_set_invalid_field(dev, scmd, fp, 0xff);
	return 1;

#ifdef MY_ABC_HERE
 skip_cmd:
	ata_dev_printk(dev, KERN_ERR, "skip command 0x%x feature 0x%x", tf->command, tf->feature);
	if (cdb[2] & 0x20) {
		ata_gen_passthru_sense(qc);
	}
	return 1;
#endif /* MY_ABC_HERE */
}

/**
 * ata_format_dsm_trim_descr() - SATL Write Same to DSM Trim
 * @cmd: SCSI command being translated
 * @trmax: Maximum number of entries that will fit in sector_size bytes.
 * @sector: Starting sector
 * @count: Total Range of request in logical sectors
 *
 * Rewrite the WRITE SAME descriptor to be a DSM TRIM little-endian formatted
 * descriptor.
 *
 * Upto 64 entries of the format:
 *   63:48 Range Length
 *   47:0  LBA
 *
 *  Range Length of 0 is ignored.
 *  LBA's should be sorted order and not overlap.
 *
 * NOTE: this is the same format as ADD LBA(S) TO NV CACHE PINNED SET
 *
 * Return: Number of bytes copied into sglist.
 */
static size_t ata_format_dsm_trim_descr(struct scsi_cmnd *cmd, u32 trmax,
					u64 sector, u32 count)
{
	struct scsi_device *sdp = cmd->device;
	size_t len = sdp->sector_size;
	size_t r;
	__le64 *buf;
	u32 i = 0;
	unsigned long flags;

	WARN_ON(len > ATA_SCSI_RBUF_SIZE);

	if (len > ATA_SCSI_RBUF_SIZE)
		len = ATA_SCSI_RBUF_SIZE;

	spin_lock_irqsave(&ata_scsi_rbuf_lock, flags);
	buf = ((void *)ata_scsi_rbuf);
	memset(buf, 0, len);
	while (i < trmax) {
		u64 entry = sector |
			((u64)(count > 0xffff ? 0xffff : count) << 48);
		buf[i++] = __cpu_to_le64(entry);
		if (count <= 0xffff)
			break;
		count -= 0xffff;
		sector += 0xffff;
	}
	r = sg_copy_from_buffer(scsi_sglist(cmd), scsi_sg_count(cmd), buf, len);
	spin_unlock_irqrestore(&ata_scsi_rbuf_lock, flags);

	return r;
}

/**
 * ata_scsi_write_same_xlat() - SATL Write Same to ATA SCT Write Same
 * @qc: Command to be translated
 *
 * Translate a SCSI WRITE SAME command to be either a DSM TRIM command or
 * an SCT Write Same command.
 * Based on WRITE SAME has the UNMAP flag:
 *
 *   - When set translate to DSM TRIM
 *   - When clear translate to SCT Write Same
 */
static unsigned int ata_scsi_write_same_xlat(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct scsi_device *sdp = scmd->device;
	size_t len = sdp->sector_size;
	struct ata_device *dev = qc->dev;
	const u8 *cdb = scmd->cmnd;
	u64 block;
	u32 n_block;
	const u32 trmax = len >> 3;
	u32 size;
	u16 fp;
	u8 bp = 0xff;
	u8 unmap = cdb[1] & 0x8;

	/* we may not issue DMA commands if no DMA mode is set */
	if (unlikely(!dev->dma_mode))
		goto invalid_opcode;

	/*
	 * We only allow sending this command through the block layer,
	 * as it modifies the DATA OUT buffer, which would corrupt user
	 * memory for SG_IO commands.
	 */
	if (unlikely(blk_rq_is_passthrough(scmd->request)))
		goto invalid_opcode;

	if (unlikely(scmd->cmd_len < 16)) {
		fp = 15;
		goto invalid_fld;
	}
	scsi_16_lba_len(cdb, &block, &n_block);

	if (!unmap ||
	    (dev->horkage & ATA_HORKAGE_NOTRIM) ||
	    !ata_id_has_trim(dev->id)) {
		fp = 1;
		bp = 3;
		goto invalid_fld;
	}
	/* If the request is too large the cmd is invalid */
	if (n_block > 0xffff * trmax) {
		fp = 2;
		goto invalid_fld;
	}

	/*
	 * WRITE SAME always has a sector sized buffer as payload, this
	 * should never be a multiple entry S/G list.
	 */
	if (!scsi_sg_count(scmd))
		goto invalid_param_len;

	/*
	 * size must match sector size in bytes
	 * For DATA SET MANAGEMENT TRIM in ACS-2 nsect (aka count)
	 * is defined as number of 512 byte blocks to be transferred.
	 */

	size = ata_format_dsm_trim_descr(scmd, trmax, block, n_block);
	if (size != len)
		goto invalid_param_len;

	if (ata_ncq_enabled(dev) && ata_fpdma_dsm_supported(dev)) {
		/* Newer devices support queued TRIM commands */
		tf->protocol = ATA_PROT_NCQ;
		tf->command = ATA_CMD_FPDMA_SEND;
		tf->hob_nsect = ATA_SUBCMD_FPDMA_SEND_DSM & 0x1f;
		tf->nsect = qc->hw_tag << 3;
		tf->hob_feature = (size / 512) >> 8;
		tf->feature = size / 512;

		tf->auxiliary = 1;
	} else {
		tf->protocol = ATA_PROT_DMA;
		tf->hob_feature = 0;
		tf->feature = ATA_DSM_TRIM;
		tf->hob_nsect = (size / 512) >> 8;
		tf->nsect = size / 512;
		tf->command = ATA_CMD_DSM;
	}

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE | ATA_TFLAG_LBA48 |
		     ATA_TFLAG_WRITE;

	ata_qc_set_pc_nbytes(qc);

	return 0;

invalid_fld:
	ata_scsi_set_invalid_field(dev, scmd, fp, bp);
	return 1;
invalid_param_len:
	/* "Parameter list length error" */
	ata_scsi_set_sense(dev, scmd, ILLEGAL_REQUEST, 0x1a, 0x0);
	return 1;
invalid_opcode:
	/* "Invalid command operation code" */
	ata_scsi_set_sense(dev, scmd, ILLEGAL_REQUEST, 0x20, 0x0);
	return 1;
}

/**
 *	ata_scsiop_maint_in - Simulate a subset of MAINTENANCE_IN
 *	@args: device MAINTENANCE_IN data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *
 *	Yields a subset to satisfy scsi_report_opcode()
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsiop_maint_in(struct ata_scsi_args *args, u8 *rbuf)
{
	struct ata_device *dev = args->dev;
	u8 *cdb = args->cmd->cmnd;
	u8 supported = 0;
	unsigned int err = 0;

	if (cdb[2] != 1) {
		ata_dev_warn(dev, "invalid command format %d\n", cdb[2]);
		err = 2;
		goto out;
	}
	switch (cdb[3]) {
	case INQUIRY:
	case MODE_SENSE:
	case MODE_SENSE_10:
	case READ_CAPACITY:
	case SERVICE_ACTION_IN_16:
	case REPORT_LUNS:
	case REQUEST_SENSE:
	case SYNCHRONIZE_CACHE:
	case REZERO_UNIT:
	case SEEK_6:
	case SEEK_10:
	case TEST_UNIT_READY:
	case SEND_DIAGNOSTIC:
	case MAINTENANCE_IN:
	case READ_6:
	case READ_10:
	case READ_16:
	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
	case ATA_12:
	case ATA_16:
	case VERIFY:
	case VERIFY_16:
	case MODE_SELECT:
	case MODE_SELECT_10:
	case START_STOP:
		supported = 3;
		break;
	case ZBC_IN:
	case ZBC_OUT:
		if (ata_id_zoned_cap(dev->id) ||
		    dev->class == ATA_DEV_ZAC)
			supported = 3;
		break;
	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		if (dev->flags & ATA_DFLAG_TRUSTED)
			supported = 3;
		break;
	default:
		break;
	}
out:
	rbuf[1] = supported; /* supported */
	return err;
}

/**
 *	ata_scsi_report_zones_complete - convert ATA output
 *	@qc: command structure returning the data
 *
 *	Convert T-13 little-endian field representation into
 *	T-10 big-endian field representation.
 *	What a mess.
 */
static void ata_scsi_report_zones_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct sg_mapping_iter miter;
	unsigned long flags;
	unsigned int bytes = 0;

	sg_miter_start(&miter, scsi_sglist(scmd), scsi_sg_count(scmd),
		       SG_MITER_TO_SG | SG_MITER_ATOMIC);

	local_irq_save(flags);
	while (sg_miter_next(&miter)) {
		unsigned int offset = 0;

		if (bytes == 0) {
			char *hdr;
			u32 list_length;
			u64 max_lba, opt_lba;
			u16 same;

			/* Swizzle header */
			hdr = miter.addr;
			list_length = get_unaligned_le32(&hdr[0]);
			same = get_unaligned_le16(&hdr[4]);
			max_lba = get_unaligned_le64(&hdr[8]);
			opt_lba = get_unaligned_le64(&hdr[16]);
			put_unaligned_be32(list_length, &hdr[0]);
			hdr[4] = same & 0xf;
			put_unaligned_be64(max_lba, &hdr[8]);
			put_unaligned_be64(opt_lba, &hdr[16]);
			offset += 64;
			bytes += 64;
		}
		while (offset < miter.length) {
			char *rec;
			u8 cond, type, non_seq, reset;
			u64 size, start, wp;

			/* Swizzle zone descriptor */
			rec = miter.addr + offset;
			type = rec[0] & 0xf;
			cond = (rec[1] >> 4) & 0xf;
			non_seq = (rec[1] & 2);
			reset = (rec[1] & 1);
			size = get_unaligned_le64(&rec[8]);
			start = get_unaligned_le64(&rec[16]);
			wp = get_unaligned_le64(&rec[24]);
			rec[0] = type;
			rec[1] = (cond << 4) | non_seq | reset;
			put_unaligned_be64(size, &rec[8]);
			put_unaligned_be64(start, &rec[16]);
			put_unaligned_be64(wp, &rec[24]);
			WARN_ON(offset + 64 > miter.length);
			offset += 64;
			bytes += 64;
		}
	}
	sg_miter_stop(&miter);
	local_irq_restore(flags);

	ata_scsi_qc_complete(qc);
}

static unsigned int ata_scsi_zbc_in_xlat(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	u16 sect, fp = (u16)-1;
	u8 sa, options, bp = 0xff;
	u64 block;
	u32 n_block;

	if (unlikely(scmd->cmd_len < 16)) {
		ata_dev_warn(qc->dev, "invalid cdb length %d\n",
			     scmd->cmd_len);
		fp = 15;
		goto invalid_fld;
	}
	scsi_16_lba_len(cdb, &block, &n_block);
	if (n_block != scsi_bufflen(scmd)) {
		ata_dev_warn(qc->dev, "non-matching transfer count (%d/%d)\n",
			     n_block, scsi_bufflen(scmd));
		goto invalid_param_len;
	}
	sa = cdb[1] & 0x1f;
	if (sa != ZI_REPORT_ZONES) {
		ata_dev_warn(qc->dev, "invalid service action %d\n", sa);
		fp = 1;
		goto invalid_fld;
	}
	/*
	 * ZAC allows only for transfers in 512 byte blocks,
	 * and uses a 16 bit value for the transfer count.
	 */
	if ((n_block / 512) > 0xffff || n_block < 512 || (n_block % 512)) {
		ata_dev_warn(qc->dev, "invalid transfer count %d\n", n_block);
		goto invalid_param_len;
	}
	sect = n_block / 512;
	options = cdb[14] & 0xbf;

	if (ata_ncq_enabled(qc->dev) &&
	    ata_fpdma_zac_mgmt_in_supported(qc->dev)) {
		tf->protocol = ATA_PROT_NCQ;
		tf->command = ATA_CMD_FPDMA_RECV;
		tf->hob_nsect = ATA_SUBCMD_FPDMA_RECV_ZAC_MGMT_IN & 0x1f;
		tf->nsect = qc->hw_tag << 3;
		tf->feature = sect & 0xff;
		tf->hob_feature = (sect >> 8) & 0xff;
		tf->auxiliary = ATA_SUBCMD_ZAC_MGMT_IN_REPORT_ZONES | (options << 8);
	} else {
		tf->command = ATA_CMD_ZAC_MGMT_IN;
		tf->feature = ATA_SUBCMD_ZAC_MGMT_IN_REPORT_ZONES;
		tf->protocol = ATA_PROT_DMA;
		tf->hob_feature = options;
		tf->hob_nsect = (sect >> 8) & 0xff;
		tf->nsect = sect & 0xff;
	}
	tf->device = ATA_LBA;
	tf->lbah = (block >> 16) & 0xff;
	tf->lbam = (block >> 8) & 0xff;
	tf->lbal = block & 0xff;
	tf->hob_lbah = (block >> 40) & 0xff;
	tf->hob_lbam = (block >> 32) & 0xff;
	tf->hob_lbal = (block >> 24) & 0xff;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE | ATA_TFLAG_LBA48;
	qc->flags |= ATA_QCFLAG_RESULT_TF;

	ata_qc_set_pc_nbytes(qc);

	qc->complete_fn = ata_scsi_report_zones_complete;

	return 0;

invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, bp);
	return 1;

invalid_param_len:
	/* "Parameter list length error" */
	ata_scsi_set_sense(qc->dev, scmd, ILLEGAL_REQUEST, 0x1a, 0x0);
	return 1;
}

static unsigned int ata_scsi_zbc_out_xlat(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	const u8 *cdb = scmd->cmnd;
	u8 all, sa;
	u64 block;
	u32 n_block;
	u16 fp = (u16)-1;

	if (unlikely(scmd->cmd_len < 16)) {
		fp = 15;
		goto invalid_fld;
	}

	sa = cdb[1] & 0x1f;
	if ((sa != ZO_CLOSE_ZONE) && (sa != ZO_FINISH_ZONE) &&
	    (sa != ZO_OPEN_ZONE) && (sa != ZO_RESET_WRITE_POINTER)) {
		fp = 1;
		goto invalid_fld;
	}

	scsi_16_lba_len(cdb, &block, &n_block);
	if (n_block) {
		/*
		 * ZAC MANAGEMENT OUT doesn't define any length
		 */
		goto invalid_param_len;
	}

	all = cdb[14] & 0x1;
	if (all) {
		/*
		 * Ignore the block address (zone ID) as defined by ZBC.
		 */
		block = 0;
	} else if (block >= dev->n_sectors) {
		/*
		 * Block must be a valid zone ID (a zone start LBA).
		 */
		fp = 2;
		goto invalid_fld;
	}

	if (ata_ncq_enabled(qc->dev) &&
	    ata_fpdma_zac_mgmt_out_supported(qc->dev)) {
		tf->protocol = ATA_PROT_NCQ_NODATA;
		tf->command = ATA_CMD_NCQ_NON_DATA;
		tf->feature = ATA_SUBCMD_NCQ_NON_DATA_ZAC_MGMT_OUT;
		tf->nsect = qc->hw_tag << 3;
		tf->auxiliary = sa | ((u16)all << 8);
	} else {
		tf->protocol = ATA_PROT_NODATA;
		tf->command = ATA_CMD_ZAC_MGMT_OUT;
		tf->feature = sa;
		tf->hob_feature = all;
	}
	tf->lbah = (block >> 16) & 0xff;
	tf->lbam = (block >> 8) & 0xff;
	tf->lbal = block & 0xff;
	tf->hob_lbah = (block >> 40) & 0xff;
	tf->hob_lbam = (block >> 32) & 0xff;
	tf->hob_lbal = (block >> 24) & 0xff;
	tf->device = ATA_LBA;
	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE | ATA_TFLAG_LBA48;

	return 0;

 invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, 0xff);
	return 1;
invalid_param_len:
	/* "Parameter list length error" */
	ata_scsi_set_sense(qc->dev, scmd, ILLEGAL_REQUEST, 0x1a, 0x0);
	return 1;
}

/**
 *	ata_mselect_caching - Simulate MODE SELECT for caching info page
 *	@qc: Storage for translated ATA taskfile
 *	@buf: input buffer
 *	@len: number of valid bytes in the input buffer
 *	@fp: out parameter for the failed field on error
 *
 *	Prepare a taskfile to modify caching information for the device.
 *
 *	LOCKING:
 *	None.
 */
static int ata_mselect_caching(struct ata_queued_cmd *qc,
			       const u8 *buf, int len, u16 *fp)
{
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;
	u8 mpage[CACHE_MPAGE_LEN];
	u8 wce;
	int i;

	/*
	 * The first two bytes of def_cache_mpage are a header, so offsets
	 * in mpage are off by 2 compared to buf.  Same for len.
	 */

	if (len != CACHE_MPAGE_LEN - 2) {
		if (len < CACHE_MPAGE_LEN - 2)
			*fp = len;
		else
			*fp = CACHE_MPAGE_LEN - 2;
		return -EINVAL;
	}

	wce = buf[0] & (1 << 2);

	/*
	 * Check that read-only bits are not modified.
	 */
	ata_msense_caching(dev->id, mpage, false);
	for (i = 0; i < CACHE_MPAGE_LEN - 2; i++) {
		if (i == 0)
			continue;
		if (mpage[i + 2] != buf[i]) {
			*fp = i;
			return -EINVAL;
		}
	}

	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf->protocol = ATA_PROT_NODATA;
	tf->nsect = 0;
	tf->command = ATA_CMD_SET_FEATURES;
	tf->feature = wce ? SETFEATURES_WC_ON : SETFEATURES_WC_OFF;
	return 0;
}

/**
 *	ata_mselect_control - Simulate MODE SELECT for control page
 *	@qc: Storage for translated ATA taskfile
 *	@buf: input buffer
 *	@len: number of valid bytes in the input buffer
 *	@fp: out parameter for the failed field on error
 *
 *	Prepare a taskfile to modify caching information for the device.
 *
 *	LOCKING:
 *	None.
 */
static int ata_mselect_control(struct ata_queued_cmd *qc,
			       const u8 *buf, int len, u16 *fp)
{
	struct ata_device *dev = qc->dev;
	u8 mpage[CONTROL_MPAGE_LEN];
	u8 d_sense;
	int i;

	/*
	 * The first two bytes of def_control_mpage are a header, so offsets
	 * in mpage are off by 2 compared to buf.  Same for len.
	 */

	if (len != CONTROL_MPAGE_LEN - 2) {
		if (len < CONTROL_MPAGE_LEN - 2)
			*fp = len;
		else
			*fp = CONTROL_MPAGE_LEN - 2;
		return -EINVAL;
	}

	d_sense = buf[0] & (1 << 2);

	/*
	 * Check that read-only bits are not modified.
	 */
	ata_msense_control(dev, mpage, false);
	for (i = 0; i < CONTROL_MPAGE_LEN - 2; i++) {
		if (i == 0)
			continue;
		if (mpage[2 + i] != buf[i]) {
			*fp = i;
			return -EINVAL;
		}
	}
	if (d_sense & (1 << 2))
		dev->flags |= ATA_DFLAG_D_SENSE;
	else
		dev->flags &= ~ATA_DFLAG_D_SENSE;
	return 0;
}

/**
 *	ata_scsi_mode_select_xlat - Simulate MODE SELECT 6, 10 commands
 *	@qc: Storage for translated ATA taskfile
 *
 *	Converts a MODE SELECT command to an ATA SET FEATURES taskfile.
 *	Assume this is invoked for direct access devices (e.g. disks) only.
 *	There should be no block descriptor for other device types.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static unsigned int ata_scsi_mode_select_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	u8 pg, spg;
	unsigned six_byte, pg_len, hdr_len, bd_len;
	int len;
	u16 fp = (u16)-1;
	u8 bp = 0xff;
	u8 buffer[64];
	const u8 *p = buffer;

	VPRINTK("ENTER\n");

	six_byte = (cdb[0] == MODE_SELECT);
	if (six_byte) {
		if (scmd->cmd_len < 5) {
			fp = 4;
			goto invalid_fld;
		}

		len = cdb[4];
		hdr_len = 4;
	} else {
		if (scmd->cmd_len < 9) {
			fp = 8;
			goto invalid_fld;
		}

		len = (cdb[7] << 8) + cdb[8];
		hdr_len = 8;
	}

	/* We only support PF=1, SP=0.  */
	if ((cdb[1] & 0x11) != 0x10) {
		fp = 1;
		bp = (cdb[1] & 0x01) ? 1 : 5;
		goto invalid_fld;
	}

	/* Test early for possible overrun.  */
	if (!scsi_sg_count(scmd) || scsi_sglist(scmd)->length < len)
		goto invalid_param_len;

	/* Move past header and block descriptors.  */
	if (len < hdr_len)
		goto invalid_param_len;

	if (!sg_copy_to_buffer(scsi_sglist(scmd), scsi_sg_count(scmd),
			       buffer, sizeof(buffer)))
		goto invalid_param_len;

	if (six_byte)
		bd_len = p[3];
	else
		bd_len = (p[6] << 8) + p[7];

	len -= hdr_len;
	p += hdr_len;
	if (len < bd_len)
		goto invalid_param_len;
	if (bd_len != 0 && bd_len != 8) {
		fp = (six_byte) ? 3 : 6;
		fp += bd_len + hdr_len;
		goto invalid_param;
	}

	len -= bd_len;
	p += bd_len;
	if (len == 0)
		goto skip;

	/* Parse both possible formats for the mode page headers.  */
	pg = p[0] & 0x3f;
	if (p[0] & 0x40) {
		if (len < 4)
			goto invalid_param_len;

		spg = p[1];
		pg_len = (p[2] << 8) | p[3];
		p += 4;
		len -= 4;
	} else {
		if (len < 2)
			goto invalid_param_len;

		spg = 0;
		pg_len = p[1];
		p += 2;
		len -= 2;
	}

	/*
	 * No mode subpages supported (yet) but asking for _all_
	 * subpages may be valid
	 */
	if (spg && (spg != ALL_SUB_MPAGES)) {
		fp = (p[0] & 0x40) ? 1 : 0;
		fp += hdr_len + bd_len;
		goto invalid_param;
	}
	if (pg_len > len)
		goto invalid_param_len;

	switch (pg) {
	case CACHE_MPAGE:
		if (ata_mselect_caching(qc, p, pg_len, &fp) < 0) {
			fp += hdr_len + bd_len;
			goto invalid_param;
		}
		break;
	case CONTROL_MPAGE:
		if (ata_mselect_control(qc, p, pg_len, &fp) < 0) {
			fp += hdr_len + bd_len;
			goto invalid_param;
		} else {
			goto skip; /* No ATA command to send */
		}
		break;
	default:		/* invalid page code */
		fp = bd_len + hdr_len;
		goto invalid_param;
	}

	/*
	 * Only one page has changeable data, so we only support setting one
	 * page at a time.
	 */
	if (len > pg_len)
		goto invalid_param;

	return 0;

 invalid_fld:
	ata_scsi_set_invalid_field(qc->dev, scmd, fp, bp);
	return 1;

 invalid_param:
	ata_scsi_set_invalid_parameter(qc->dev, scmd, fp);
	return 1;

 invalid_param_len:
	/* "Parameter list length error" */
	ata_scsi_set_sense(qc->dev, scmd, ILLEGAL_REQUEST, 0x1a, 0x0);
	return 1;

 skip:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}

static u8 ata_scsi_trusted_op(u32 len, bool send, bool dma)
{
	if (len == 0)
		return ATA_CMD_TRUSTED_NONDATA;
	else if (send)
		return dma ? ATA_CMD_TRUSTED_SND_DMA : ATA_CMD_TRUSTED_SND;
	else
		return dma ? ATA_CMD_TRUSTED_RCV_DMA : ATA_CMD_TRUSTED_RCV;
}

static unsigned int ata_scsi_security_inout_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	struct ata_taskfile *tf = &qc->tf;
	u8 secp = cdb[1];
	bool send = (cdb[0] == SECURITY_PROTOCOL_OUT);
	u16 spsp = get_unaligned_be16(&cdb[2]);
	u32 len = get_unaligned_be32(&cdb[6]);
	bool dma = !(qc->dev->flags & ATA_DFLAG_PIO);

	/*
	 * We don't support the ATA "security" protocol.
	 */
	if (secp == 0xef) {
		ata_scsi_set_invalid_field(qc->dev, scmd, 1, 0);
		return 1;
	}

	if (cdb[4] & 7) { /* INC_512 */
		if (len > 0xffff) {
			ata_scsi_set_invalid_field(qc->dev, scmd, 6, 0);
			return 1;
		}
	} else {
		if (len > 0x01fffe00) {
			ata_scsi_set_invalid_field(qc->dev, scmd, 6, 0);
			return 1;
		}

		/* convert to the sector-based ATA addressing */
		len = (len + 511) / 512;
	}

	tf->protocol = dma ? ATA_PROT_DMA : ATA_PROT_PIO;
	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR | ATA_TFLAG_LBA;
	if (send)
		tf->flags |= ATA_TFLAG_WRITE;
	tf->command = ata_scsi_trusted_op(len, send, dma);
	tf->feature = secp;
	tf->lbam = spsp & 0xff;
	tf->lbah = spsp >> 8;

	if (len) {
		tf->nsect = len & 0xff;
		tf->lbal = len >> 8;
	} else {
		if (!send)
			tf->lbah = (1 << 7);
	}

	ata_qc_set_pc_nbytes(qc);
	return 0;
}

/**
 *	ata_scsi_var_len_cdb_xlat - SATL variable length CDB to Handler
 *	@qc: Command to be translated
 *
 *	Translate a SCSI variable length CDB to specified commands.
 *	It checks a service action value in CDB to call corresponding handler.
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure
 *
 */
static unsigned int ata_scsi_var_len_cdb_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	const u16 sa = get_unaligned_be16(&cdb[8]);

	/*
	 * if service action represents a ata pass-thru(32) command,
	 * then pass it to ata_scsi_pass_thru handler.
	 */
	if (sa == ATA_32)
		return ata_scsi_pass_thru(qc);

	/* unsupported service action */
	return 1;
}

/**
 *	ata_get_xlat_func - check if SCSI to ATA translation is possible
 *	@dev: ATA device
 *	@cmd: SCSI command opcode to consider
 *
 *	Look up the SCSI command given, and determine whether the
 *	SCSI command is to be translated or simulated.
 *
 *	RETURNS:
 *	Pointer to translation function if possible, %NULL if not.
 */

static inline ata_xlat_func_t ata_get_xlat_func(struct ata_device *dev, u8 cmd)
{
	switch (cmd) {
	case READ_6:
	case READ_10:
	case READ_16:

	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
		return ata_scsi_rw_xlat;

	case WRITE_SAME_16:
		return ata_scsi_write_same_xlat;

	case SYNCHRONIZE_CACHE:
		if (ata_try_flush_cache(dev))
			return ata_scsi_flush_xlat;
		break;

	case VERIFY:
	case VERIFY_16:
		return ata_scsi_verify_xlat;

	case ATA_12:
	case ATA_16:
		return ata_scsi_pass_thru;

	case VARIABLE_LENGTH_CMD:
		return ata_scsi_var_len_cdb_xlat;

	case MODE_SELECT:
	case MODE_SELECT_10:
		return ata_scsi_mode_select_xlat;
		break;

	case ZBC_IN:
		return ata_scsi_zbc_in_xlat;

	case ZBC_OUT:
		return ata_scsi_zbc_out_xlat;

	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		if (!(dev->flags & ATA_DFLAG_TRUSTED))
			break;
		return ata_scsi_security_inout_xlat;

	case START_STOP:
		return ata_scsi_start_stop_xlat;
	}

	return NULL;
}

/**
 *	ata_scsi_dump_cdb - dump SCSI command contents to dmesg
 *	@ap: ATA port to which the command was being sent
 *	@cmd: SCSI command to dump
 *
 *	Prints the contents of a SCSI command via printk().
 */

void ata_scsi_dump_cdb(struct ata_port *ap, struct scsi_cmnd *cmd)
{
#ifdef ATA_VERBOSE_DEBUG
	struct scsi_device *scsidev = cmd->device;

	VPRINTK("CDB (%u:%d,%d,%lld) %9ph\n",
		ap->print_id,
		scsidev->channel, scsidev->id, scsidev->lun,
		cmd->cmnd);
#endif
}

#ifdef MY_ABC_HERE
/* Action after prehook */
typedef enum _tag_ATACMD_HOOK_ACTION {
	ACT_BYPASS = 0,
	ACT_SKIP,
	ACT_REISSUE,
} ATACMD_HOOK_ACTION;

/* Check before cmd xlat */
static ATACMD_HOOK_ACTION syno_ata_scsi_xlat_prehook(struct ata_device *dev, struct scsi_cmnd *scmd)
{
	ATACMD_HOOK_ACTION ret = ACT_BYPASS;


#ifdef MY_ABC_HERE
	/*
	 * EUnit GPIO control commands can be directly issued
	 */
	if (ata_is_host_link(dev->link) && (dev->link->ap->link.uiStsFlags & SYNO_STATUS_GPIO_CTRL)) {
		return ACT_BYPASS;
	}

	/* if irq off, we must Schedule Wake Up immediately to let
	 * ResubmitCommand(..) -> ata_port_schedule_eh(..) -> ata_scsi_error(..)
	 * power on disk */
	if ((dev->link->ap->pflags & ATA_PFLAG_SYNO_IRQ_OFF) ||
		(dev->link->ap->pflags & ATA_PFLAG_SYNO_IRQOFF_PWROFF_DONE)) {

		if (!(dev->link->ap->pflags & ATA_PFLAG_SYNO_DS_PWROFF)) {
			ata_port_schedule_eh(dev->link->ap);
			return ACT_REISSUE;
		} else {
			return ACT_SKIP;
		}
	}

	if(IS_SYNOLOGY_RX1223RP(dev->link->ap->PMSynoUnique)) {
		if ((dev->link->ap->pflags & ATA_PFLAG_SYNO_DS_WAKING)) {
			ata_port_schedule_eh(dev->link->ap);
			return ACT_REISSUE;
		}
	}

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (dev->link->ap->nr_pmp_links && dev->link->ap->pflags & ATA_PFLAG_SYNO_BOOT_PROBE) {
		/* I don't know why some EUnit master may not clear ATA_PFLAG_SYNO_BOOT_PROBE,
		 * so we must clear it again by schedule_eh*/
		ata_port_schedule_eh(dev->link->ap);
		return ACT_REISSUE;
	}
#endif /* MY_ABC_HERE */

	return ret;
}

/* Something after cmd xlat */
static void syno_ata_scsi_xlat_posthook(struct ata_device *dev, struct scsi_cmnd *scmd, int rc)
{
#ifdef MY_ABC_HERE
	static unsigned long iStuckTimeout;
	static int icPMRWDefer = 0;
	struct ata_queued_cmd *active_qc;
	u8 active_command;

	/* This was the original work around for the problem that PMP GPIO command stucked in the low level driver and cause in system hang.
	 * Though the issue has been fixed, we leave it here to make sure the system will not hang when running into a similar situation.
	 * When multiple commands are deferred in a row longer than normal ATA command timeout(10 sec),
	 * and the command occupying the lower level queue is an PMP R/W command, we force it to abort.
	 * Also, we assumed that there won't be more than 64 commands(twice of the default ATA queue depth)
	 * be deffered in common cases. */
	active_qc = __ata_qc_from_tag(dev->link->ap, 0);
	active_command = active_qc->tf.command;
	/* we abort the PMP R/W command if it stuck in ata queue too long and caused too many defer */
	if (SCSI_MLQUEUE_DEVICE_BUSY != rc && SCSI_MLQUEUE_HOST_BUSY != rc){
		icPMRWDefer = 0;
		iStuckTimeout = jiffies + 10 * HZ;
	} else if (64 <= icPMRWDefer &&
			  time_after_eq(jiffies, iStuckTimeout) &&
			  active_qc->flags & ATA_QCFLAG_ACTIVE &&
			  (ATA_CMD_PMP_READ == active_command || ATA_CMD_PMP_WRITE == active_command)) {
		icPMRWDefer = 0;
		iStuckTimeout = jiffies + 10 * HZ;
		ata_dev_printk(dev, KERN_INFO,"Abort stucked PMP R/W command\n");
		ata_port_abort(dev->link->ap);
	} else {
		icPMRWDefer++;
	}
#endif /* MY_ABC_HERE */

	return ;
}
#endif /* MY_ABC_HERE */

int __ata_scsi_queuecmd(struct scsi_cmnd *scmd, struct ata_device *dev)
{
	u8 scsi_op = scmd->cmnd[0];
	ata_xlat_func_t xlat_func;
	int rc = 0;

	if (dev->class == ATA_DEV_ATA || dev->class == ATA_DEV_ZAC) {
		if (unlikely(!scmd->cmd_len || scmd->cmd_len > dev->cdb_len))
			goto bad_cdb_len;

		xlat_func = ata_get_xlat_func(dev, scsi_op);
	} else {
		if (unlikely(!scmd->cmd_len))
			goto bad_cdb_len;

		xlat_func = NULL;
		if (likely((scsi_op != ATA_16) || !atapi_passthru16)) {
			/* relay SCSI command to ATAPI device */
			int len = COMMAND_SIZE(scsi_op);
			if (unlikely(len > scmd->cmd_len ||
				     len > dev->cdb_len ||
				     scmd->cmd_len > ATAPI_CDB_LEN))
				goto bad_cdb_len;

			xlat_func = atapi_xlat;
		} else {
			/* ATA_16 passthru, treat as an ATA command */
			if (unlikely(scmd->cmd_len > 16))
				goto bad_cdb_len;

			xlat_func = ata_get_xlat_func(dev, scsi_op);
		}
	}

#ifdef MY_ABC_HERE
	if (xlat_func) {
		ATACMD_HOOK_ACTION hook = syno_ata_scsi_xlat_prehook(dev, scmd);

		if (ACT_REISSUE == hook) {
			return SCSI_MLQUEUE_HOST_BUSY;
		} else if (ACT_SKIP == hook) {
			goto SKIP;
		}
		/* else should be ACT_BYPASS, just bypass */
	}
#endif /* MY_ABC_HERE */

	if (xlat_func)
#ifdef MY_DEF_HERE
	{
		if (0 == gSynoHddPowerupSeq && 1 == guiWakeupDisksNum) {
			/* no spin up delay, use original function */
			rc = ata_scsi_translate(dev, scmd, xlat_func);
		} else {
			/* command translate with spin up delay */
			rc = syno_ata_scsi_translate(dev, scmd, xlat_func);
		}
	}
#else /* MY_DEF_HERE */
		rc = ata_scsi_translate(dev, scmd, xlat_func);
#endif /* MY_DEF_HERE */
	else
		ata_scsi_simulate(dev, scmd);

#ifdef MY_ABC_HERE
SKIP:
	syno_ata_scsi_xlat_posthook(dev, scmd, rc);
#endif /* MY_ABC_HERE */

	return rc;

 bad_cdb_len:
	DPRINTK("bad CDB len=%u, scsi_op=0x%02x, max=%u\n",
		scmd->cmd_len, scsi_op, dev->cdb_len);
	scmd->result = DID_ERROR << 16;
	scmd->scsi_done(scmd);
	return 0;
}

/**
 *	ata_scsi_queuecmd - Issue SCSI cdb to libata-managed device
 *	@shost: SCSI host of command to be sent
 *	@cmd: SCSI command to be sent
 *
 *	In some cases, this function translates SCSI commands into
 *	ATA taskfiles, and queues the taskfiles to be sent to
 *	hardware.  In other cases, this function simulates a
 *	SCSI device by evaluating and responding to certain
 *	SCSI commands.  This creates the overall effect of
 *	ATA and ATAPI devices appearing as SCSI devices.
 *
 *	LOCKING:
 *	ATA host lock
 *
 *	RETURNS:
 *	Return value from __ata_scsi_queuecmd() if @cmd can be queued,
 *	0 otherwise.
 */
int ata_scsi_queuecmd(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct ata_port *ap;
	struct ata_device *dev;
	struct scsi_device *scsidev = cmd->device;
	int rc = 0;
	unsigned long irq_flags;

	ap = ata_shost_to_port(shost);

	spin_lock_irqsave(ap->lock, irq_flags);

	ata_scsi_dump_cdb(ap, cmd);

	dev = ata_scsi_find_dev(ap, scsidev);
	if (likely(dev))
		rc = __ata_scsi_queuecmd(cmd, dev);
	else {
		cmd->result = (DID_BAD_TARGET << 16);
		cmd->scsi_done(cmd);
	}

	spin_unlock_irqrestore(ap->lock, irq_flags);

	return rc;
}
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);

/**
 *	ata_scsi_simulate - simulate SCSI command on ATA device
 *	@dev: the target device
 *	@cmd: SCSI command being sent to device.
 *
 *	Interprets and directly executes a select list of SCSI commands
 *	that can be handled internally.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

void ata_scsi_simulate(struct ata_device *dev, struct scsi_cmnd *cmd)
{
	struct ata_scsi_args args;
	const u8 *scsicmd = cmd->cmnd;
	u8 tmp8;

	args.dev = dev;
	args.id = dev->id;
	args.cmd = cmd;

	switch(scsicmd[0]) {
	case INQUIRY:
		if (scsicmd[1] & 2)		   /* is CmdDt set?  */
			ata_scsi_set_invalid_field(dev, cmd, 1, 0xff);
		else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_std);
		else switch (scsicmd[2]) {
		case 0x00:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_00);
			break;
		case 0x80:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_80);
			break;
		case 0x83:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_83);
			break;
		case 0x89:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_89);
			break;
		case 0xb0:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_b0);
			break;
		case 0xb1:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_b1);
			break;
		case 0xb2:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_b2);
			break;
		case 0xb6:
			if (dev->flags & ATA_DFLAG_ZAC) {
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_b6);
				break;
			}
			fallthrough;
		default:
			ata_scsi_set_invalid_field(dev, cmd, 2, 0xff);
			break;
		}
		break;

	case MODE_SENSE:
	case MODE_SENSE_10:
		ata_scsi_rbuf_fill(&args, ata_scsiop_mode_sense);
		break;

	case READ_CAPACITY:
		ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
		break;

	case SERVICE_ACTION_IN_16:
		if ((scsicmd[1] & 0x1f) == SAI_READ_CAPACITY_16)
			ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
		else
			ata_scsi_set_invalid_field(dev, cmd, 1, 0xff);
		break;

	case REPORT_LUNS:
		ata_scsi_rbuf_fill(&args, ata_scsiop_report_luns);
		break;

	case REQUEST_SENSE:
		ata_scsi_set_sense(dev, cmd, 0, 0, 0);
		cmd->result = (DRIVER_SENSE << 24);
		break;

	/* if we reach this, then writeback caching is disabled,
	 * turning this into a no-op.
	 */
	case SYNCHRONIZE_CACHE:
		fallthrough;

	/* no-op's, complete with success */
	case REZERO_UNIT:
	case SEEK_6:
	case SEEK_10:
	case TEST_UNIT_READY:
		break;

	case SEND_DIAGNOSTIC:
		tmp8 = scsicmd[1] & ~(1 << 3);
		if (tmp8 != 0x4 || scsicmd[3] || scsicmd[4])
			ata_scsi_set_invalid_field(dev, cmd, 1, 0xff);
		break;

	case MAINTENANCE_IN:
		if (scsicmd[1] == MI_REPORT_SUPPORTED_OPERATION_CODES)
			ata_scsi_rbuf_fill(&args, ata_scsiop_maint_in);
		else
			ata_scsi_set_invalid_field(dev, cmd, 1, 0xff);
		break;

	/* all other commands */
	default:
		ata_scsi_set_sense(dev, cmd, ILLEGAL_REQUEST, 0x20, 0x0);
		/* "Invalid command operation code" */
		break;
	}

	cmd->scsi_done(cmd);
}

int ata_scsi_add_hosts(struct ata_host *host, struct scsi_host_template *sht)
{
	int i, rc;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		struct Scsi_Host *shost;

		rc = -ENOMEM;
		shost = scsi_host_alloc(sht, sizeof(struct ata_port *));
		if (!shost)
			goto err_alloc;

		shost->eh_noresume = 1;
		*(struct ata_port **)&shost->hostdata[0] = ap;
		ap->scsi_host = shost;

		shost->transportt = ata_scsi_transport_template;
		shost->unique_id = ap->print_id;
		shost->max_id = 16;
		shost->max_lun = 1;
		shost->max_channel = 1;
		shost->max_cmd_len = 32;

		/* Schedule policy is determined by ->qc_defer()
		 * callback and it needs to see every deferred qc.
		 * Set host_blocked to 1 to prevent SCSI midlayer from
		 * automatically deferring requests.
		 */
		shost->max_host_blocked = 1;

		rc = scsi_add_host_with_dma(shost, &ap->tdev, ap->host->dev);
		if (rc)
			goto err_alloc;
	}

	return 0;

 err_alloc:
	while (--i >= 0) {
		struct Scsi_Host *shost = host->ports[i]->scsi_host;

		/* scsi_host_put() is in ata_devres_release() */
		scsi_remove_host(shost);
	}
	return rc;
}

#ifdef CONFIG_OF
static void ata_scsi_assign_ofnode(struct ata_device *dev, struct ata_port *ap)
{
	struct scsi_device *sdev = dev->sdev;
	struct device *d = ap->host->dev;
	struct device_node *np = d->of_node;
	struct device_node *child;

	for_each_available_child_of_node(np, child) {
		int ret;
		u32 val;

		ret = of_property_read_u32(child, "reg", &val);
		if (ret)
			continue;
		if (val == dev->devno) {
			dev_dbg(d, "found matching device node\n");
			sdev->sdev_gendev.of_node = child;
			return;
		}
	}
}
#else
static void ata_scsi_assign_ofnode(struct ata_device *dev, struct ata_port *ap)
{
}
#endif

void ata_scsi_scan_host(struct ata_port *ap, int sync)
{
	int tries = 5;
	struct ata_device *last_failed_dev = NULL;
	struct ata_link *link;
	struct ata_device *dev;
#ifdef MY_ABC_HERE
	char modelbuf[ATA_ID_PROD_LEN+1];
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	struct scsi_device *pPmSdev;
	int iPmId = 0;

	if (syno_is_synology_pm(ap)) {
		dev = (struct ata_device *)ap->link.device;
		iPmId = dev->devno;
		pPmSdev = __scsi_add_device(ap->scsi_host, SYNO_PM_VIRTUAL_SCSI_CHANNEL, iPmId, 0,
				 NULL);

		if (!IS_ERR(pPmSdev)) {
			dev->sdev = pPmSdev;
			scsi_device_put(pPmSdev);
			syno_pm_show_sn(dev);
		} else {
			dev->sdev = NULL;
		}
	}
#endif /* MY_ABC_HERE */

 repeat:
	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ENABLED) {
			struct scsi_device *sdev;
			int channel = 0, id = 0;

			if (dev->sdev)
				continue;

			if (ata_is_host_link(link))
				id = dev->devno;
			else
				channel = link->pmp;

#ifdef MY_ABC_HERE
			if (dev->is_ssd) {
				ata_id_c_string(dev->id, modelbuf, ATA_ID_PROD, sizeof(modelbuf));
				ata_dev_printk(dev, KERN_WARNING, "Find SSD disks. [%s]\n", modelbuf);
			}
#endif /* MY_ABC_HERE */

			sdev = __scsi_add_device(ap->scsi_host, channel, id, 0,
						 NULL);
			if (!IS_ERR(sdev)) {
				dev->sdev = sdev;
				ata_scsi_assign_ofnode(dev, ap);
				scsi_device_put(sdev);
			} else {
				dev->sdev = NULL;
			}
		}
	}

	/* If we scanned while EH was in progress or allocation
	 * failure occurred, scan would have failed silently.  Check
	 * whether all devices are attached.
	 */
	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ENABLED) {
			if (!dev->sdev)
				goto exit_loop;
		}
	}
 exit_loop:
	if (!link)
		return;

	/* we're missing some SCSI devices */
	if (sync) {
		/* If caller requested synchrnous scan && we've made
		 * any progress, sleep briefly and repeat.
		 */
		if (dev != last_failed_dev) {
			msleep(100);
			last_failed_dev = dev;
			goto repeat;
		}

		/* We might be failing to detect boot device, give it
		 * a few more chances.
		 */
		if (--tries) {
			msleep(100);
			goto repeat;
		}

		ata_port_err(ap,
			     "WARNING: synchronous SCSI scan failed without making any progress, switching to async\n");
	}

	queue_delayed_work(system_long_wq, &ap->hotplug_task,
			   round_jiffies_relative(HZ));
}

/**
 *	ata_scsi_offline_dev - offline attached SCSI device
 *	@dev: ATA device to offline attached SCSI device for
 *
 *	This function is called from ata_eh_hotplug() and responsible
 *	for taking the SCSI device attached to @dev offline.  This
 *	function is called with host lock which protects dev->sdev
 *	against clearing.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	1 if attached SCSI device exists, 0 otherwise.
 */
int ata_scsi_offline_dev(struct ata_device *dev)
{
	if (dev->sdev) {
		scsi_device_set_state(dev->sdev, SDEV_OFFLINE);
		return 1;
	}
	return 0;
}

/**
 *	ata_scsi_remove_dev - remove attached SCSI device
 *	@dev: ATA device to remove attached SCSI device for
 *
 *	This function is called from ata_eh_scsi_hotplug() and
 *	responsible for removing the SCSI device attached to @dev.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_scsi_remove_dev(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;
	struct scsi_device *sdev;
	unsigned long flags;

	/* Alas, we need to grab scan_mutex to ensure SCSI device
	 * state doesn't change underneath us and thus
	 * scsi_device_get() always succeeds.  The mutex locking can
	 * be removed if there is __scsi_device_get() interface which
	 * increments reference counts regardless of device state.
	 */
	mutex_lock(&ap->scsi_host->scan_mutex);
	spin_lock_irqsave(ap->lock, flags);

	/* clearing dev->sdev is protected by host lock */
	sdev = dev->sdev;
	dev->sdev = NULL;

	if (sdev) {
		/* If user initiated unplug races with us, sdev can go
		 * away underneath us after the host lock and
		 * scan_mutex are released.  Hold onto it.
		 */
		if (scsi_device_get(sdev) == 0) {
			/* The following ensures the attached sdev is
			 * offline on return from ata_scsi_offline_dev()
			 * regardless it wins or loses the race
			 * against this function.
			 */
			scsi_device_set_state(sdev, SDEV_OFFLINE);
		} else {
			WARN_ON(1);
			sdev = NULL;
		}
	}

	spin_unlock_irqrestore(ap->lock, flags);
	mutex_unlock(&ap->scsi_host->scan_mutex);

	if (sdev) {
		ata_dev_info(dev, "detaching (SCSI %s)\n",
			     dev_name(&sdev->sdev_gendev));

		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
}

static void ata_scsi_handle_link_detach(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_device *dev;

	ata_for_each_dev(dev, link, ALL) {
		unsigned long flags;

		if (!(dev->flags & ATA_DFLAG_DETACHED))
			continue;

		spin_lock_irqsave(ap->lock, flags);
		dev->flags &= ~ATA_DFLAG_DETACHED;
		spin_unlock_irqrestore(ap->lock, flags);

		if (zpodd_dev_enabled(dev))
			zpodd_exit(dev);

		ata_scsi_remove_dev(dev);
	}
}

/**
 *	ata_scsi_media_change_notify - send media change event
 *	@dev: Pointer to the disk device with media change event
 *
 *	Tell the block layer to send a media change notification
 *	event.
 *
 * 	LOCKING:
 * 	spin_lock_irqsave(host lock)
 */
void ata_scsi_media_change_notify(struct ata_device *dev)
{
	if (dev->sdev)
		sdev_evt_send_simple(dev->sdev, SDEV_EVT_MEDIA_CHANGE,
				     GFP_ATOMIC);
}

/**
 *	ata_scsi_hotplug - SCSI part of hotplug
 *	@work: Pointer to ATA port to perform SCSI hotplug on
 *
 *	Perform SCSI part of hotplug.  It's executed from a separate
 *	workqueue after EH completes.  This is necessary because SCSI
 *	hot plugging requires working EH and hot unplugging is
 *	synchronized with hot plugging with a mutex.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_scsi_hotplug(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, hotplug_task.work);
	int i;
#ifdef MY_ABC_HERE
	char *envp[2];
#endif /* MY_ABC_HERE */

	if (ap->pflags & ATA_PFLAG_UNLOADING) {
		DPRINTK("ENTER/EXIT - unloading\n");
		return;
	}

	DPRINTK("ENTER\n");
	mutex_lock(&ap->scsi_scan_mutex);

	/* Unplug detached devices.  We cannot use link iterator here
	 * because PMP links have to be scanned even if PMP is
	 * currently not attached.  Iterate manually.
	 */
	ata_scsi_handle_link_detach(&ap->link);
	if (ap->pmp_link)
		for (i = 0; i < SATA_PMP_MAX_PORTS; i++)
			ata_scsi_handle_link_detach(&ap->pmp_link[i]);

	/* scan for new ones */
	ata_scsi_scan_host(ap, 0);

#ifdef MY_ABC_HERE
	if (ap->pflags & ATA_PFLAG_PMP_DISCONNECT) {
		envp[0] = SZK_PMP_UEVENT"="SZV_PMP_DISCONNECT;
		ap->pflags &= ~ATA_PFLAG_PMP_DISCONNECT;
	} else if (ap->pflags & ATA_PFLAG_PMP_CONNECT) {
		envp[0] = SZK_PMP_UEVENT"="SZV_PMP_CONNECT;
		ap->pflags &= ~ATA_PFLAG_PMP_CONNECT;
	} else {
		envp[0] = NULL;
	}

	envp[1] = NULL;
	kobject_uevent_env(&ap->scsi_host->shost_dev.kobj, KOBJ_CHANGE, envp);
#endif /* MY_ABC_HERE */

	mutex_unlock(&ap->scsi_scan_mutex);
	DPRINTK("EXIT\n");
}

#ifdef MY_ABC_HERE
void ata_syno_pmp_hotplug(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, hotplug_task.work);
	char *envp[2];

	if (ap->pflags & ATA_PFLAG_PMP_DISCONNECT) {
		envp[0] = SZK_PMP_UEVENT"="SZV_PMP_DISCONNECT;
		ap->pflags &= ~ATA_PFLAG_PMP_DISCONNECT;
	} else if (ap->pflags & ATA_PFLAG_PMP_CONNECT) {
		envp[0] = SZK_PMP_UEVENT"="SZV_PMP_CONNECT;
		ap->pflags &= ~ATA_PFLAG_PMP_CONNECT;
	} else {
		envp[0] = NULL;
	}

	envp[1] = NULL;
	kobject_uevent_env(&ap->scsi_host->shost_dev.kobj, KOBJ_CHANGE, envp);
}
#endif /* MY_ABC_HERE */

/**
 *	ata_scsi_user_scan - indication for user-initiated bus scan
 *	@shost: SCSI host to scan
 *	@channel: Channel to scan
 *	@id: ID to scan
 *	@lun: LUN to scan
 *
 *	This function is called when user explicitly requests bus
 *	scan.  Set probe pending flag and invoke EH.
 *
 *	LOCKING:
 *	SCSI layer (we don't care)
 *
 *	RETURNS:
 *	Zero.
 */
int ata_scsi_user_scan(struct Scsi_Host *shost, unsigned int channel,
		       unsigned int id, u64 lun)
{
	struct ata_port *ap = ata_shost_to_port(shost);
	unsigned long flags;
	int devno, rc = 0;

	if (!ap->ops->error_handler)
		return -EOPNOTSUPP;

	if (lun != SCAN_WILD_CARD && lun)
		return -EINVAL;

	if (!sata_pmp_attached(ap)) {
		if (channel != SCAN_WILD_CARD && channel)
			return -EINVAL;
		devno = id;
	} else {
		if (id != SCAN_WILD_CARD && id)
			return -EINVAL;
		devno = channel;
	}

	spin_lock_irqsave(ap->lock, flags);

	if (devno == SCAN_WILD_CARD) {
		struct ata_link *link;

		ata_for_each_link(link, ap, EDGE) {
			struct ata_eh_info *ehi = &link->eh_info;
			ehi->probe_mask |= ATA_ALL_DEVICES;
			ehi->action |= ATA_EH_RESET;
		}
	} else {
		struct ata_device *dev = ata_find_dev(ap, devno);

		if (dev) {
			struct ata_eh_info *ehi = &dev->link->eh_info;
			ehi->probe_mask |= 1 << dev->devno;
			ehi->action |= ATA_EH_RESET;
		} else
			rc = -EINVAL;
	}

	if (rc == 0) {
		ata_port_schedule_eh(ap);
		spin_unlock_irqrestore(ap->lock, flags);
		ata_port_wait_eh(ap);
	} else
		spin_unlock_irqrestore(ap->lock, flags);

	return rc;
}

/**
 *	ata_scsi_dev_rescan - initiate scsi_rescan_device()
 *	@work: Pointer to ATA port to perform scsi_rescan_device()
 *
 *	After ATA pass thru (SAT) commands are executed successfully,
 *	libata need to propagate the changes to SCSI layer.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_scsi_dev_rescan(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, scsi_rescan_task);
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long flags;

	mutex_lock(&ap->scsi_scan_mutex);
	spin_lock_irqsave(ap->lock, flags);

	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ENABLED) {
			struct scsi_device *sdev = dev->sdev;

			if (!sdev)
				continue;
			if (scsi_device_get(sdev))
				continue;

			spin_unlock_irqrestore(ap->lock, flags);
			scsi_rescan_device(&(sdev->sdev_gendev));
			scsi_device_put(sdev);
			spin_lock_irqsave(ap->lock, flags);
		}
	}

	spin_unlock_irqrestore(ap->lock, flags);
	mutex_unlock(&ap->scsi_scan_mutex);
}

#ifdef MY_ABC_HERE
static struct scsi_device * syno_look_up_scsi_dev_from_ata_link(struct ata_link *pAtaLink)
{
	struct scsi_device *pScsiDevice = NULL;
	struct ata_device *pAtaDevicedev = NULL;

	if (NULL == pAtaLink) {
		goto END;
	}

	ata_for_each_dev(pAtaDevicedev, pAtaLink, ALL) {
		if (pAtaDevicedev->sdev && SDEV_RUNNING == pAtaDevicedev->sdev->sdev_state) {
			pScsiDevice = pAtaDevicedev->sdev;
			break;
		}
	}
END:
	return pScsiDevice;
}

int syno_libata_numeric_diskname_number_get(struct ata_link *pAtaLink)
{
	struct scsi_device *pScsiDevice = syno_look_up_scsi_dev_from_ata_link(pAtaLink);
	int iSynoDiskNameNumber = 0, iRet = -1;
	char *pSynoDiskNameNumber = NULL;

	if (NULL == pAtaLink || NULL == pScsiDevice) {
		goto END;
	}

	pSynoDiskNameNumber = strstr(pScsiDevice->syno_disk_name, CONFIG_SYNO_SATA_DEVICE_PREFIX);
	if (NULL == pSynoDiskNameNumber) {
		printk(KERN_INFO "Disk name [%s] is not a %s disk name.\n", pScsiDevice->syno_disk_name, CONFIG_SYNO_SATA_DEVICE_PREFIX);
		goto END;
	}

	if (0 != kstrtoint(pSynoDiskNameNumber + strlen(CONFIG_SYNO_SATA_DEVICE_PREFIX), 10, &iSynoDiskNameNumber)) {
		printk(KERN_INFO "Disk name [%s] cannot convert its disk number.\n", pScsiDevice->syno_disk_name);
		goto END;
	}
	iRet = iSynoDiskNameNumber;
END:
	return iRet;
}
EXPORT_SYMBOL(syno_libata_numeric_diskname_number_get);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
int syno_external_libata_index_get(const struct ata_port *ap)
{
	int index = -1;
	struct device_node *pDeviceNode = NULL;
	if (NULL == ap || NULL == of_root) {
		goto END;
	}

	for_each_child_of_node(of_root, pDeviceNode) {
		if (pDeviceNode->full_name
			&& 0 == (strncmp(pDeviceNode->full_name, DT_ESATA_SLOT, strlen(DT_ESATA_SLOT)))) {
			if (true == ap->ops->syno_compare_node_info(ap, pDeviceNode)) {
				// get index number of esata_slot, e.g. /esata_slot@4 --> 4
				sscanf(pDeviceNode->full_name, DT_ESATA_SLOT"@%d", &index);
				of_node_put(pDeviceNode);
				break;
			}
		} else if (pDeviceNode->full_name
			&& 0 == (strncmp(pDeviceNode->full_name, DT_CX4_SLOT, strlen(DT_CX4_SLOT)))) {
				if (true == ap->ops->syno_compare_node_info(ap, pDeviceNode)) {
				// get index number of cx4_slot, e.g. /cx4_slot@4, 1 --> 4
				sscanf(pDeviceNode->full_name, DT_CX4_SLOT"@%d", &index);
				of_node_put(pDeviceNode);
				break;
			}
		}
	}
END:
	return index;
}
EXPORT_SYMBOL(syno_external_libata_index_get);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size);
static void syno_pciepath_enum(struct device *dev, char *buf) {
	struct pci_dev *pdev = NULL;
	char sztemp[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};

	if (NULL == buf || NULL == dev) {
		return;
	}
	pdev = to_pci_dev(dev);

	if (-1 == syno_pciepath_dts_pattern_get(pdev, sztemp, sizeof(sztemp))) {
		return;
	}

	if (NULL != sztemp) {
		snprintf(buf, BLOCK_INFO_SIZE, "%spciepath=%s\n", buf, sztemp);
	}
}

static void syno_ata_info_enum(struct ata_port *ap, struct scsi_device *sdev) {
#ifdef MY_ABC_HERE
	struct ata_device *dev = NULL;
#endif /* MY_ABC_HERE */

	if (NULL == ap || NULL == sdev || NULL == ap->host) {
		return;
	}

	snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sata_port_no=%u\n", sdev->syno_block_info, ap->port_no);

#ifdef MY_ABC_HERE
	dev = ata_scsi_find_dev(ap, sdev);
	if (syno_is_synology_pm(ap) && NULL != dev && NULL != dev->link) {
		snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sis_syno_pmp=1\n", sdev->syno_block_info);
		snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%spmp_link=%u\n", sdev->syno_block_info, dev->link->pmp);
		snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sEMID=%u\n", sdev->syno_block_info, ap->PMSynoEMID);

		if (IS_SYNOLOGY_DX517(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_DX517);
		} else if (IS_SYNOLOGY_RX418(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_RX418);
		} else if (IS_SYNOLOGY_RX1217(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_RX1217);
		} else if (IS_SYNOLOGY_DX1215(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_DX1215);
		} else if (IS_SYNOLOGY_DX1222(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_DX1222);
		} else if (IS_SYNOLOGY_DX1215II(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_DX1215II);
		} else if (IS_SYNOLOGY_RX1223RP(ap->PMSynoUnique)) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sunique=%s\n", sdev->syno_block_info, EBOX_INFO_UNIQUE_RX1223RP);
		}
	}
#endif /* MY_ABC_HERE */
}

void syno_libata_info_enum(struct scsi_device *sdev) {
	struct ata_port *ap = NULL;

	ap = ata_shost_to_port(sdev->host);

	if (NULL != ap) {
		if (ap->dev->bus && !strcmp("pci", ap->dev->bus->name)) {
			syno_pciepath_enum(ap->dev, sdev->syno_block_info);
		}
		syno_ata_info_enum(ap, sdev);
		if (ap->scsi_host->hostt && ap->scsi_host->hostt->name) {
			snprintf(sdev->syno_block_info, BLOCK_INFO_SIZE, "%sdriver=%s\n", sdev->syno_block_info, ap->scsi_host->hostt->name);
		}
	}
}
EXPORT_SYMBOL(syno_libata_info_enum);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static ssize_t syno_wcache_show(struct device *device,
				  struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap;
	struct ata_device *dev;
	unsigned long flags;
	int rc = 0;

	ap = ata_shost_to_port(sdev->host);

	spin_lock_irqsave(ap->lock, flags);
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev) {
		rc = -ENODEV;
		goto unlock;
	}

	if (dev->class != ATA_DEV_ATA) {
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	if (dev->flags & ATA_DFLAG_NO_WCACHE) {
		rc = snprintf(buf, 20, "%s\n", "wcache_disable");
	} else {
		rc = snprintf(buf, 20, "%s\n", "wcache_enable");
	}

unlock:
	spin_unlock_irq(ap->lock);

	return rc;
}

static ssize_t syno_wcache_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];
	unsigned char model_rev[ATA_ID_FW_REV_LEN + 1];
	struct ata_blacklist_entry *ad = ata_device_blacklist;
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap;
	struct ata_device *dev;
	long int input;
	unsigned long flags;
	int rc;

	rc = kstrtol_from_user(buf, len, 10, &input);
	if (rc)
		return -EINVAL;

	ap = ata_shost_to_port(sdev->host);

	spin_lock_irqsave(ap->lock, flags);
	dev = ata_scsi_find_dev(ap, sdev);
	if (unlikely(!dev)) {
		rc = -ENODEV;
		goto unlock;
	}
	if (dev->class != ATA_DEV_ATA) {
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	/* FIXME: Because we can't poweroff EUnit disks separately.
	 * So we can't let EUnit control wcache flag now */
	if (ap->nr_pmp_links) {
		DBGMESG("ata%u: we can't let EUnit control wcache through this path now\n", ap->print_id);
		goto unlock;
	}

	// update ata_device_blacklist
	ata_id_c_string(dev->id, model_num, ATA_ID_PROD, sizeof(model_num));
	ata_id_c_string(dev->id, model_rev, ATA_ID_FW_REV, sizeof(model_rev));
	while (ad->model_num) {
		if (glob_match(ad->model_num, model_num)) {
			if (ad->model_rev == NULL || glob_match(ad->model_rev, model_rev)) {
				if (input) {
					ad->horkage &= ~ATA_HORKAGE_NOWCACHE;
				} else {
					ad->horkage |= ATA_HORKAGE_NOWCACHE;
				}
			}
		}
		ad++;
	}

	if (!input) {
		if (dev->flags & ATA_DFLAG_NO_WCACHE) {
			rc = 0;
			goto unlock;
		}

		dev->link->eh_info.dev_action[dev->devno] |= ATA_EH_WCACHE_DISABLE;
		dev->flags |= ATA_DFLAG_NO_WCACHE;
		dev->horkage |= ATA_HORKAGE_NOWCACHE;
		ata_port_schedule_eh(ap);
	} else {
		dev->flags &= ~ATA_DFLAG_NO_WCACHE;
		dev->horkage &= ~ATA_HORKAGE_NOWCACHE;
	}

unlock:
	spin_unlock_irqrestore(ap->lock, flags);

	return rc ? rc : len;
}
DEVICE_ATTR(syno_wcache, S_IRUGO | S_IWUSR,
	    syno_wcache_show, syno_wcache_store);
EXPORT_SYMBOL_GPL(dev_attr_syno_wcache);
#endif /* MY_ABC_HERE */
