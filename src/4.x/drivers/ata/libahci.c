#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 *  libahci.c - Common AHCI SATA low-level routines
 *
 *  Maintained by:  Tejun Heo <tj@kernel.org>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2004-2005 Red Hat, Inc.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * libata documentation is available via 'make {ps|pdf}docs',
 * as Documentation/DocBook/libata.*
 *
 * AHCI hardware documentation:
 * http://www.intel.com/technology/serialata/pdf/rev1_0.pdf
 * http://www.intel.com/technology/serialata/pdf/rev1_1.pdf
 *
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#ifdef MY_DEF_HERE
#include <scsi/scsi_device.h>
#endif /* MY_DEF_HERE */
#include <linux/libata.h>
#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE) || defined(MY_ABC_HERE)
#include <linux/pci.h>
#include <linux/leds.h>
#endif /* defined(MY_ABC_HERE) || defined(MY_DEF_HERE) || defined(MY_ABC_HERE) */
#include "ahci.h"
#include "libata.h"

#ifdef MY_DEF_HERE
#include <linux/pci.h>
int (*syno_ata_qc_complete_multiple)(struct ata_port *ap, u32 qc_active) = NULL;
EXPORT_SYMBOL(syno_ata_qc_complete_multiple);
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
extern void syno_ledtrig_active_set(int iLedNum);
extern int *gpGreenLedMap;
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
#include <linux/synolib.h>
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
#include <linux/synolib.h>
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
#include <linux/syno_gpio.h>
#endif /* MY_DEF_HERE */

#if defined(MY_DEF_HERE)
#ifdef MY_ABC_HERE
#include <linux/syno_gpio.h>
#define SYNO_LED_BLINK_OFF 0
#define SYNO_LED_BLINK_ON 1
static int syno_set_blink(struct ata_port* ap, u32 value);
#else /* MY_ABC_HERE */
extern int SYNO_CTRL_GPIO_HDD_ACT_LED(int index, int value);
#endif /* MY_ABC_HERE */
#endif

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
#ifdef MY_ABC_HERE
static u32 syno_get_prop_sw_activity(struct ata_port* ap);
#endif /* MY_ABC_HERE */
#endif /* defined(MY_ABC_HERE) || defined(MY_DEF_HERE) */

static int ahci_skip_host_reset;
int ahci_ignore_sss;
EXPORT_SYMBOL_GPL(ahci_ignore_sss);

module_param_named(skip_host_reset, ahci_skip_host_reset, int, 0444);
MODULE_PARM_DESC(skip_host_reset, "skip global host reset (0=don't skip, 1=skip)");

module_param_named(ignore_sss, ahci_ignore_sss, int, 0444);
MODULE_PARM_DESC(ignore_sss, "Ignore staggered spinup flag (0=don't ignore, 1=ignore)");

static int ahci_set_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			unsigned hints);
static ssize_t ahci_led_show(struct ata_port *ap, char *buf);
static ssize_t ahci_led_store(struct ata_port *ap, const char *buf,
			      size_t size);
#ifdef MY_DEF_HERE
static ssize_t ahci_syno_present_transmit_led_message(struct ata_port *ap, u32 state,
					ssize_t size);
#else /* MY_DEF_HERE */
static ssize_t ahci_transmit_led_message(struct ata_port *ap, u32 state,
					ssize_t size);
#endif /* MY_DEF_HERE */



static int ahci_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val);
static int ahci_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val);
static bool ahci_qc_fill_rtf(struct ata_queued_cmd *qc);
static int ahci_port_start(struct ata_port *ap);
static void ahci_port_stop(struct ata_port *ap);
static enum ata_completion_errors ahci_qc_prep(struct ata_queued_cmd *qc);
static int ahci_pmp_qc_defer(struct ata_queued_cmd *qc);
static void ahci_freeze(struct ata_port *ap);
static void ahci_thaw(struct ata_port *ap);
static void ahci_set_aggressive_devslp(struct ata_port *ap, bool sleep);
static void ahci_enable_fbs(struct ata_port *ap);
static void ahci_disable_fbs(struct ata_port *ap);
static void ahci_pmp_attach(struct ata_port *ap);
static void ahci_pmp_detach(struct ata_port *ap);
static int ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline);
static int ahci_pmp_retry_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline);
static int ahci_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline);
static void ahci_postreset(struct ata_link *link, unsigned int *class);
static void ahci_post_internal_cmd(struct ata_queued_cmd *qc);
static void ahci_dev_config(struct ata_device *dev);
#ifdef CONFIG_PM
static int ahci_port_suspend(struct ata_port *ap, pm_message_t mesg);
#endif
static ssize_t ahci_activity_show(struct ata_device *dev, char *buf);
static ssize_t ahci_activity_store(struct ata_device *dev,
				   enum sw_activity val);
static void ahci_init_sw_activity(struct ata_link *link);

static ssize_t ahci_show_host_caps(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t ahci_show_host_cap2(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t ahci_show_host_version(struct device *dev,
				      struct device_attribute *attr, char *buf);
static ssize_t ahci_show_port_cmd(struct device *dev,
				  struct device_attribute *attr, char *buf);
#ifdef MY_DEF_HERE
static void syno_internal_ahci_handle_port_interrupt(struct ata_port *ap,
				       void __iomem *port_mmio, u32 status);
static void ahci_handle_port_interrupt(struct ata_port *ap,
				       void __iomem *port_mmio, u32 status);
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
static void ahci_port_intr(struct ata_port *ap);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
static irqreturn_t ahci_multi_irqs_intr(int irq, void *dev_instance);
static irqreturn_t syno_ahci_multi_irqs_intr_jmb(int irq, void *dev_instance);
#ifdef MY_DEF_HERE
static irqreturn_t ahci_port_thread_fn(int irq, void *dev_instance);
static irqreturn_t syno_ahci_multi_hardirqs_intr_jmb(int irq, void *dev_instance);
static irqreturn_t (*syno_ahci_port_thread_fn)(int, void *) = NULL;
#endif /* MY_DEF_HERE */
static irqreturn_t (*syno_ahci_multi_irqs_intr)(int, void *);
#endif /* MY_ABC_HERE */
static ssize_t ahci_read_em_buffer(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t ahci_store_em_buffer(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);
static ssize_t ahci_show_em_supported(struct device *dev,
				      struct device_attribute *attr, char *buf);

static DEVICE_ATTR(ahci_host_caps, S_IRUGO, ahci_show_host_caps, NULL);
static DEVICE_ATTR(ahci_host_cap2, S_IRUGO, ahci_show_host_cap2, NULL);
static DEVICE_ATTR(ahci_host_version, S_IRUGO, ahci_show_host_version, NULL);
static DEVICE_ATTR(ahci_port_cmd, S_IRUGO, ahci_show_port_cmd, NULL);
#ifdef MY_DEF_HERE
static ssize_t
ata_ahci_locate_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[atadev->link->pmp];

	return sprintf(buf, "%ld\n", emp->locate);
}

static void ahci_sw_locate_set(struct ata_link *link, u8 blEnable);

static ssize_t
ata_ahci_locate_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);
	int val;

	if (ap->flags & ATA_FLAG_SW_LOCATE) {
		val = simple_strtoul(buf, NULL, 0);
		switch (val) {
		case 0:
		case 1:
			ahci_sw_locate_set(atadev->link, val);
			return count;
		default:
			return -EIO;
		}
	}
	return -EINVAL;
}
DEVICE_ATTR(sw_locate, S_IWUSR | S_IRUGO, ata_ahci_locate_show, ata_ahci_locate_store);

static ssize_t
ata_ahci_fault_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[atadev->link->pmp];

	return sprintf(buf, "%ld\n", emp->fault);
}

static void ahci_sw_fault_set(struct ata_link *link, u8 blEnable);

#ifdef MY_ABC_HERE
/**
 * sata_syno_ahci_diskled_set_by_port - control led of slot by ahci gpio
 * @iDiskPort [IN]: slot number
 * @iPresent [IN]:  Present status
 * @iFault [IN]:    Fault status
 *
 * return void
 */
void sata_syno_ahci_diskled_set_by_port(int iDiskPort, int iPresent, int iFault)
{
	struct ata_port *pAp = NULL;
	pAp = syno_ata_port_get_by_port(iDiskPort);
	if (pAp->nr_pmp_links) {
		goto END;
	}
	ata_for_each_link(pAtaLink, pAp, EDGE) {
		ata_for_each_dev(pAtaDev, pAtaLink, ALL) {
			//set disk LED
			ahci_sw_locate_set(pAtaDev->link, iPresent);
			ahci_sw_fault_set(pAtaDev->link, iFault);
		}
	}
END:
	return;
}
EXPORT_SYMBOL(sata_syno_ahci_diskled_set_by_port);
#endif /* MY_ABC_HERE */

void sata_syno_ahci_diskled_set(int iHostNum, int iPresent, int iFault)
{
	struct ata_port *pAp = NULL;
	struct ata_device *pAtaDev = NULL;
	struct Scsi_Host *pScsiHost = NULL;
	struct ata_link *pAtaLink = NULL;

	if (NULL == (pScsiHost = scsi_host_lookup(iHostNum))) {
			goto END;
	}

	//get port from SCSI host
	pAp = ata_shost_to_port(pScsiHost);
	if (!pAp) {
		goto RELEASESRC;
	}

	//get rid of pmp ports
	if (pAp->nr_pmp_links) {
		goto RELEASESRC;
	}

	//for each devices of each links in port ap
	ata_for_each_link(pAtaLink, pAp, EDGE) {
		ata_for_each_dev(pAtaDev, pAtaLink, ALL) {
			//set disk LED
			ahci_sw_locate_set(pAtaDev->link, iPresent);
			ahci_sw_fault_set(pAtaDev->link, iFault);
		}
	}

RELEASESRC:
	scsi_host_put(pScsiHost);
END:
	return;
}
EXPORT_SYMBOL(sata_syno_ahci_diskled_set);

static ssize_t
ata_ahci_fault_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);
	int val;

	if (ap->flags & ATA_FLAG_SW_FAULT) {
		val = simple_strtoul(buf, NULL, 0);
		switch (val) {
		case 0:
		case 1:
			ahci_sw_fault_set(atadev->link, val);
			return count;
		default:
			return -EIO;
		}
	}
	return -EINVAL;
}
DEVICE_ATTR(sw_fault, S_IWUSR | S_IRUGO, ata_ahci_fault_show, ata_ahci_fault_store);
#endif /* MY_DEF_HERE */

static DEVICE_ATTR(em_buffer, S_IWUSR | S_IRUGO,
		   ahci_read_em_buffer, ahci_store_em_buffer);
static DEVICE_ATTR(em_message_supported, S_IRUGO, ahci_show_em_supported, NULL);

struct device_attribute *ahci_shost_attrs[] = {
	&dev_attr_link_power_management_policy,
	&dev_attr_em_message_type,
	&dev_attr_em_message,
	&dev_attr_ahci_host_caps,
	&dev_attr_ahci_host_cap2,
	&dev_attr_ahci_host_version,
	&dev_attr_ahci_port_cmd,
	&dev_attr_em_buffer,
	&dev_attr_em_message_supported,
#ifdef MY_ABC_HERE
	&dev_attr_syno_pm_i2c,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_manutil_power_disable,
	&dev_attr_syno_pm_gpio,
	&dev_attr_syno_pm_info,
#ifdef MY_ABC_HERE
	&dev_attr_syno_power_ctrl,
	&dev_attr_syno_pm_control_support,
#endif /* MY_ABC_HERE */
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_port_thaw,
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
	&dev_attr_syno_diskname_trans,
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_sata_disk_led_ctrl,
#endif /* MY_ABC_HERE */
	NULL
};
EXPORT_SYMBOL_GPL(ahci_shost_attrs);

struct device_attribute *ahci_sdev_attrs[] = {
	&dev_attr_sw_activity,
	&dev_attr_unload_heads,
#ifdef MY_ABC_HERE
	&dev_attr_syno_wcache,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_deep_sleep_support,
	&dev_attr_syno_deep_sleep_ctrl,
	&dev_attr_syno_pwr_reset_count,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_fake_error_ctrl,
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_sata_error_event_debug,
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
	&dev_attr_sw_locate,
	&dev_attr_sw_fault,
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
	&dev_attr_syno_disk_latency_read_hist,
	&dev_attr_syno_disk_latency_write_hist,
	&dev_attr_syno_disk_latency_other_hist,
	&dev_attr_syno_disk_latency_stat,
#ifdef MY_ABC_HERE
	&dev_attr_syno_disk_seq_stat,
#endif /* MY_ABC_HERE */
#endif /* MY_ABC_HERE */
	NULL
};
EXPORT_SYMBOL_GPL(ahci_sdev_attrs);

struct ata_port_operations ahci_ops = {
	.inherits		= &sata_pmp_port_ops,

	.qc_defer		= ahci_pmp_qc_defer,
	.qc_prep		= ahci_qc_prep,
	.qc_issue		= ahci_qc_issue,
	.qc_fill_rtf		= ahci_qc_fill_rtf,

	.freeze			= ahci_freeze,
	.thaw			= ahci_thaw,
	.softreset		= ahci_softreset,
	.hardreset		= ahci_hardreset,
	.postreset		= ahci_postreset,
	.pmp_softreset		= ahci_softreset,
	.error_handler		= ahci_error_handler,
	.post_internal_cmd	= ahci_post_internal_cmd,
	.dev_config		= ahci_dev_config,

	.scr_read		= ahci_scr_read,
	.scr_write		= ahci_scr_write,
	.pmp_attach		= ahci_pmp_attach,
	.pmp_detach		= ahci_pmp_detach,

	.set_lpm		= ahci_set_lpm,
	.em_show		= ahci_led_show,
	.em_store		= ahci_led_store,
	.sw_activity_show	= ahci_activity_show,
	.sw_activity_store	= ahci_activity_store,
#ifdef MY_DEF_HERE
	.transmit_led_message	= ahci_syno_present_transmit_led_message,
#else /* MY_DEF_HERE */
	.transmit_led_message	= ahci_transmit_led_message,
#endif /* MY_DEF_HERE */
#ifdef CONFIG_PM
	.port_suspend		= ahci_port_suspend,
	.port_resume		= ahci_port_resume,
#endif
	.port_start		= ahci_port_start,
	.port_stop		= ahci_port_stop,
#ifdef MY_ABC_HERE
	.syno_force_intr	= ahci_port_intr,
#endif /* MY_ABC_HERE */
};
EXPORT_SYMBOL_GPL(ahci_ops);

struct ata_port_operations ahci_pmp_retry_srst_ops = {
	.inherits		= &ahci_ops,
	.softreset		= ahci_pmp_retry_softreset,
};
EXPORT_SYMBOL_GPL(ahci_pmp_retry_srst_ops);

static bool ahci_em_messages __read_mostly = true;
module_param(ahci_em_messages, bool, 0444);
/* add other LED protocol types when they become supported */
MODULE_PARM_DESC(ahci_em_messages,
	"AHCI Enclosure Management Message control (0 = off, 1 = on)");

/* device sleep idle timeout in ms */
static int devslp_idle_timeout __read_mostly = 1000;
module_param(devslp_idle_timeout, int, 0644);
MODULE_PARM_DESC(devslp_idle_timeout, "device sleep idle timeout");

static void ahci_enable_ahci(void __iomem *mmio)
{
	int i;
	u32 tmp;

	/* turn on AHCI_EN */
	tmp = readl(mmio + HOST_CTL);
	if (tmp & HOST_AHCI_EN)
		return;

	/* Some controllers need AHCI_EN to be written multiple times.
	 * Try a few times before giving up.
	 */
	for (i = 0; i < 5; i++) {
		tmp |= HOST_AHCI_EN;
		writel(tmp, mmio + HOST_CTL);
		tmp = readl(mmio + HOST_CTL);	/* flush && sanity check */
		if (tmp & HOST_AHCI_EN)
			return;
		msleep(10);
	}

	WARN_ON(1);
}

static ssize_t ahci_show_host_caps(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;

	return sprintf(buf, "%x\n", hpriv->cap);
}

static ssize_t ahci_show_host_cap2(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;

	return sprintf(buf, "%x\n", hpriv->cap2);
}

static ssize_t ahci_show_host_version(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->mmio;

	return sprintf(buf, "%x\n", readl(mmio + HOST_VERSION));
}

static ssize_t ahci_show_port_cmd(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	void __iomem *port_mmio = ahci_port_base(ap);

	return sprintf(buf, "%x\n", readl(port_mmio + PORT_CMD));
}

static ssize_t ahci_read_em_buffer(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->mmio;
	void __iomem *em_mmio = mmio + hpriv->em_loc;
	u32 em_ctl, msg;
	unsigned long flags;
	size_t count;
	int i;

	spin_lock_irqsave(ap->lock, flags);

	em_ctl = readl(mmio + HOST_EM_CTL);
	if (!(ap->flags & ATA_FLAG_EM) || em_ctl & EM_CTL_XMT ||
	    !(hpriv->em_msg_type & EM_MSG_TYPE_SGPIO)) {
		spin_unlock_irqrestore(ap->lock, flags);
		return -EINVAL;
	}

	if (!(em_ctl & EM_CTL_MR)) {
		spin_unlock_irqrestore(ap->lock, flags);
		return -EAGAIN;
	}

	if (!(em_ctl & EM_CTL_SMB))
		em_mmio += hpriv->em_buf_sz;

	count = hpriv->em_buf_sz;

	/* the count should not be larger than PAGE_SIZE */
	if (count > PAGE_SIZE) {
		if (printk_ratelimit())
			ata_port_warn(ap,
				      "EM read buffer size too large: "
				      "buffer size %u, page size %lu\n",
				      hpriv->em_buf_sz, PAGE_SIZE);
		count = PAGE_SIZE;
	}

	for (i = 0; i < count; i += 4) {
		msg = readl(em_mmio + i);
		buf[i] = msg & 0xff;
		buf[i + 1] = (msg >> 8) & 0xff;
		buf[i + 2] = (msg >> 16) & 0xff;
		buf[i + 3] = (msg >> 24) & 0xff;
	}

	spin_unlock_irqrestore(ap->lock, flags);

	return i;
}

static ssize_t ahci_store_em_buffer(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->mmio;
	void __iomem *em_mmio = mmio + hpriv->em_loc;
	const unsigned char *msg_buf = buf;
	u32 em_ctl, msg;
	unsigned long flags;
	int i;

	/* check size validity */
	if (!(ap->flags & ATA_FLAG_EM) ||
	    !(hpriv->em_msg_type & EM_MSG_TYPE_SGPIO) ||
	    size % 4 || size > hpriv->em_buf_sz)
		return -EINVAL;

	spin_lock_irqsave(ap->lock, flags);

	em_ctl = readl(mmio + HOST_EM_CTL);
	if (em_ctl & EM_CTL_TM) {
		spin_unlock_irqrestore(ap->lock, flags);
		return -EBUSY;
	}

	for (i = 0; i < size; i += 4) {
		msg = msg_buf[i] | msg_buf[i + 1] << 8 |
		      msg_buf[i + 2] << 16 | msg_buf[i + 3] << 24;
		writel(msg, em_mmio + i);
	}

	writel(em_ctl | EM_CTL_TM, mmio + HOST_EM_CTL);

	spin_unlock_irqrestore(ap->lock, flags);

	return size;
}

static ssize_t ahci_show_em_supported(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 em_ctl;

	em_ctl = readl(mmio + HOST_EM_CTL);

	return sprintf(buf, "%s%s%s%s\n",
		       em_ctl & EM_CTL_LED ? "led " : "",
		       em_ctl & EM_CTL_SAFTE ? "saf-te " : "",
		       em_ctl & EM_CTL_SES ? "ses-2 " : "",
		       em_ctl & EM_CTL_SGPIO ? "sgpio " : "");
}

/**
 *	ahci_save_initial_config - Save and fixup initial config values
 *	@dev: target AHCI device
 *	@hpriv: host private area to store config values
 *
 *	Some registers containing configuration info might be setup by
 *	BIOS and might be cleared on reset.  This function saves the
 *	initial values of those registers into @hpriv such that they
 *	can be restored after controller reset.
 *
 *	If inconsistent, config values are fixed up by this function.
 *
 *	If it is not set already this function sets hpriv->start_engine to
 *	ahci_start_engine.
 *
 *	LOCKING:
 *	None.
 */
void ahci_save_initial_config(struct device *dev, struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = hpriv->mmio;
	u32 cap, cap2, vers, port_map;
	int i;

	/* make sure AHCI mode is enabled before accessing CAP */
	ahci_enable_ahci(mmio);

	/* Values prefixed with saved_ are written back to host after
	 * reset.  Values without are used for driver operation.
	 */
	hpriv->saved_cap = cap = readl(mmio + HOST_CAP);
	hpriv->saved_port_map = port_map = readl(mmio + HOST_PORTS_IMPL);

	/* CAP2 register is only defined for AHCI 1.2 and later */
	vers = readl(mmio + HOST_VERSION);
	if ((vers >> 16) > 1 ||
	   ((vers >> 16) == 1 && (vers & 0xFFFF) >= 0x200))
		hpriv->saved_cap2 = cap2 = readl(mmio + HOST_CAP2);
	else
		hpriv->saved_cap2 = cap2 = 0;

	/* some chips have errata preventing 64bit use */
	if ((cap & HOST_CAP_64) && (hpriv->flags & AHCI_HFLAG_32BIT_ONLY)) {
		dev_info(dev, "controller can't do 64bit DMA, forcing 32bit\n");
		cap &= ~HOST_CAP_64;
	}

	if ((cap & HOST_CAP_NCQ) && (hpriv->flags & AHCI_HFLAG_NO_NCQ)) {
		dev_info(dev, "controller can't do NCQ, turning off CAP_NCQ\n");
		cap &= ~HOST_CAP_NCQ;
	}

	if (!(cap & HOST_CAP_NCQ) && (hpriv->flags & AHCI_HFLAG_YES_NCQ)) {
		dev_info(dev, "controller can do NCQ, turning on CAP_NCQ\n");
		cap |= HOST_CAP_NCQ;
	}

	if ((cap & HOST_CAP_PMP) && (hpriv->flags & AHCI_HFLAG_NO_PMP)) {
		dev_info(dev, "controller can't do PMP, turning off CAP_PMP\n");
		cap &= ~HOST_CAP_PMP;
	}

	if ((cap & HOST_CAP_SNTF) && (hpriv->flags & AHCI_HFLAG_NO_SNTF)) {
		dev_info(dev,
			 "controller can't do SNTF, turning off CAP_SNTF\n");
		cap &= ~HOST_CAP_SNTF;
	}

	if ((cap2 & HOST_CAP2_SDS) && (hpriv->flags & AHCI_HFLAG_NO_DEVSLP)) {
		dev_info(dev,
			 "controller can't do DEVSLP, turning off\n");
		cap2 &= ~HOST_CAP2_SDS;
		cap2 &= ~HOST_CAP2_SADM;
	}

	if (!(cap & HOST_CAP_FBS) && (hpriv->flags & AHCI_HFLAG_YES_FBS)) {
		dev_info(dev, "controller can do FBS, turning on CAP_FBS\n");
		cap |= HOST_CAP_FBS;
	}

	if ((cap & HOST_CAP_FBS) && (hpriv->flags & AHCI_HFLAG_NO_FBS)) {
		dev_info(dev, "controller can't do FBS, turning off CAP_FBS\n");
		cap &= ~HOST_CAP_FBS;
	}

	if (hpriv->force_port_map && port_map != hpriv->force_port_map) {
		dev_info(dev, "forcing port_map 0x%x -> 0x%x\n",
			 port_map, hpriv->force_port_map);
		port_map = hpriv->force_port_map;
		hpriv->saved_port_map = port_map;
	}

	if (hpriv->mask_port_map) {
		dev_warn(dev, "masking port_map 0x%x -> 0x%x\n",
			port_map,
			port_map & hpriv->mask_port_map);
		port_map &= hpriv->mask_port_map;
	}

	/* cross check port_map and cap.n_ports */
	if (port_map) {
		int map_ports = 0;

		for (i = 0; i < AHCI_MAX_PORTS; i++)
			if (port_map & (1 << i))
				map_ports++;

		/* If PI has more ports than n_ports, whine, clear
		 * port_map and let it be generated from n_ports.
		 */
		if (map_ports > ahci_nr_ports(cap)) {
			dev_warn(dev,
				 "implemented port map (0x%x) contains more ports than nr_ports (%u), using nr_ports\n",
				 port_map, ahci_nr_ports(cap));
			port_map = 0;
		}
	}

	/* fabricate port_map from cap.nr_ports for < AHCI 1.3 */
	if (!port_map && vers < 0x10300) {
		port_map = (1 << ahci_nr_ports(cap)) - 1;
		dev_warn(dev, "forcing PORTS_IMPL to 0x%x\n", port_map);

		/* write the fixed up value to the PI register */
		hpriv->saved_port_map = port_map;
	}

	/* record values to use during operation */
	hpriv->cap = cap;
	hpriv->cap2 = cap2;
	hpriv->port_map = port_map;

	if (!hpriv->start_engine)
		hpriv->start_engine = ahci_start_engine;
}
EXPORT_SYMBOL_GPL(ahci_save_initial_config);

/**
 *	ahci_restore_initial_config - Restore initial config
 *	@host: target ATA host
 *
 *	Restore initial config stored by ahci_save_initial_config().
 *
 *	LOCKING:
 *	None.
 */
static void ahci_restore_initial_config(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;

	writel(hpriv->saved_cap, mmio + HOST_CAP);
	if (hpriv->saved_cap2)
		writel(hpriv->saved_cap2, mmio + HOST_CAP2);
	writel(hpriv->saved_port_map, mmio + HOST_PORTS_IMPL);
	(void) readl(mmio + HOST_PORTS_IMPL);	/* flush */
}

static unsigned ahci_scr_offset(struct ata_port *ap, unsigned int sc_reg)
{
	static const int offset[] = {
		[SCR_STATUS]		= PORT_SCR_STAT,
		[SCR_CONTROL]		= PORT_SCR_CTL,
		[SCR_ERROR]		= PORT_SCR_ERR,
		[SCR_ACTIVE]		= PORT_SCR_ACT,
		[SCR_NOTIFICATION]	= PORT_SCR_NTF,
	};
	struct ahci_host_priv *hpriv = ap->host->private_data;

	if (sc_reg < ARRAY_SIZE(offset) &&
	    (sc_reg != SCR_NOTIFICATION || (hpriv->cap & HOST_CAP_SNTF)))
		return offset[sc_reg];
	return 0;
}

static int ahci_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	int offset = ahci_scr_offset(link->ap, sc_reg);

	if (offset) {
		*val = readl(port_mmio + offset);
		return 0;
	}
	return -EINVAL;
}

static int ahci_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	int offset = ahci_scr_offset(link->ap, sc_reg);

	if (offset) {
		writel(val, port_mmio + offset);
		return 0;
	}
	return -EINVAL;
}

void ahci_start_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	/* start DMA */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD); /* flush */
}
EXPORT_SYMBOL_GPL(ahci_start_engine);

int ahci_stop_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);

	/* check if the HBA is idle */
	if ((tmp & (PORT_CMD_START | PORT_CMD_LIST_ON)) == 0)
		return 0;

	/* setting HBA to idle */
	tmp &= ~PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for engine to stop. This could be as long as 500 msec */
	tmp = ata_wait_register(ap, port_mmio + PORT_CMD,
				PORT_CMD_LIST_ON, PORT_CMD_LIST_ON, 1, 500);
	if (tmp & PORT_CMD_LIST_ON)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(ahci_stop_engine);

void ahci_start_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	u32 tmp;

	/* set FIS registers */
	if (hpriv->cap & HOST_CAP_64)
		writel((pp->cmd_slot_dma >> 16) >> 16,
		       port_mmio + PORT_LST_ADDR_HI);
	writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR);

	if (hpriv->cap & HOST_CAP_64)
		writel((pp->rx_fis_dma >> 16) >> 16,
		       port_mmio + PORT_FIS_ADDR_HI);
	writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR);

	/* enable FIS reception */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_FIS_RX;
	writel(tmp, port_mmio + PORT_CMD);

	/* flush */
	readl(port_mmio + PORT_CMD);
}
EXPORT_SYMBOL_GPL(ahci_start_fis_rx);

static int ahci_stop_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	/* disable FIS reception */
	tmp = readl(port_mmio + PORT_CMD);
	tmp &= ~PORT_CMD_FIS_RX;
	writel(tmp, port_mmio + PORT_CMD);

	/* wait for completion, spec says 500ms, give it 1000 */
	tmp = ata_wait_register(ap, port_mmio + PORT_CMD, PORT_CMD_FIS_ON,
				PORT_CMD_FIS_ON, 10, 1000);
	if (tmp & PORT_CMD_FIS_ON)
		return -EBUSY;

	return 0;
}

static void ahci_power_up(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;

	cmd = readl(port_mmio + PORT_CMD) & ~PORT_CMD_ICC_MASK;

	/* spin up device */
	if (hpriv->cap & HOST_CAP_SSS) {
		cmd |= PORT_CMD_SPIN_UP;
		writel(cmd, port_mmio + PORT_CMD);
	}

	/* wake up link */
	writel(cmd | PORT_CMD_ICC_ACTIVE, port_mmio + PORT_CMD);
}

static int ahci_set_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			unsigned int hints)
{
	struct ata_port *ap = link->ap;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);

	if (policy != ATA_LPM_MAX_POWER) {
		/*
		 * Disable interrupts on Phy Ready. This keeps us from
		 * getting woken up due to spurious phy ready
		 * interrupts.
		 */
		pp->intr_mask &= ~PORT_IRQ_PHYRDY;
		writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);

		sata_link_scr_lpm(link, policy, false);
	}

	if (hpriv->cap & HOST_CAP_ALPM) {
		u32 cmd = readl(port_mmio + PORT_CMD);

		if (policy == ATA_LPM_MAX_POWER || !(hints & ATA_LPM_HIPM)) {
			cmd &= ~(PORT_CMD_ASP | PORT_CMD_ALPE);
			cmd |= PORT_CMD_ICC_ACTIVE;

			writel(cmd, port_mmio + PORT_CMD);
			readl(port_mmio + PORT_CMD);

			/* wait 10ms to be sure we've come out of LPM state */
			ata_msleep(ap, 10);
		} else {
			cmd |= PORT_CMD_ALPE;
			if (policy == ATA_LPM_MIN_POWER)
				cmd |= PORT_CMD_ASP;

			/* write out new cmd value */
			writel(cmd, port_mmio + PORT_CMD);
		}
	}

	/* set aggressive device sleep */
	if ((hpriv->cap2 & HOST_CAP2_SDS) &&
	    (hpriv->cap2 & HOST_CAP2_SADM) &&
	    (link->device->flags & ATA_DFLAG_DEVSLP)) {
		if (policy == ATA_LPM_MIN_POWER)
			ahci_set_aggressive_devslp(ap, true);
		else
			ahci_set_aggressive_devslp(ap, false);
	}

	if (policy == ATA_LPM_MAX_POWER) {
		sata_link_scr_lpm(link, policy, false);

		/* turn PHYRDY IRQ back on */
		pp->intr_mask |= PORT_IRQ_PHYRDY;
		writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
	}

	return 0;
}

#ifdef CONFIG_PM
static void ahci_power_down(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd, scontrol;

	if (!(hpriv->cap & HOST_CAP_SSS))
		return;

	/* put device into listen mode, first set PxSCTL.DET to 0 */
	scontrol = readl(port_mmio + PORT_SCR_CTL);
	scontrol &= ~0xf;
	writel(scontrol, port_mmio + PORT_SCR_CTL);

	/* then set PxCMD.SUD to 0 */
	cmd = readl(port_mmio + PORT_CMD) & ~PORT_CMD_ICC_MASK;
	cmd &= ~PORT_CMD_SPIN_UP;
	writel(cmd, port_mmio + PORT_CMD);
}
#endif

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
static int syno_need_ahci_software_activity(struct ata_port *ap)
{
#ifdef MY_ABC_HERE
	return syno_get_prop_sw_activity(ap);
#else
	struct pci_dev *pdev = NULL;
	int ret = 0;

#ifdef MY_ABC_HERE
	if (syno_is_hw_version(HW_DS119j) || syno_is_hw_version(HW_DS120j)) {
		ret = 1;
		goto END;
	}
	/* These Avoton models do not need SW ACT */
	if (syno_is_hw_version(HW_DS2415p) || syno_is_hw_version(HW_RS2416p) || syno_is_hw_version(HW_RS2416rpp)) {
		goto END;
	}
#endif /* MY_ABC_HERE */

	if (ap && ap->dev &&
			ap->dev->bus && !strcmp("pci", ap->dev->bus->name)) {
		pdev = to_pci_dev(ap->dev);
		if (pdev != NULL && pdev->vendor == 0x8086) {
			switch (pdev->device) {
				/* Avoton internal SATA chip */
				case 0x1f22:
				case 0x1f32:
					ret = 1;
					break;
				default:
					break;
			}
		}
	}

END:
	return ret;
#endif /* MY_ABC_HERE */
}
#endif /* defined(MY_ABC_HERE) || defined(MY_DEF_HERE) */

#ifdef MY_ABC_HERE
static void syno_sw_activity(struct ata_port *ap)
{
	if (NULL == gpGreenLedMap) {
		return;
	}
	syno_ledtrig_active_set(gpGreenLedMap[ap->syno_disk_index]);
}
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
int __syno_ahci_disk_led_enable(struct ata_port *ap, const int iValue)
{
	int ret = -EINVAL;
	struct ahci_port_priv *pp = NULL;
	struct ahci_em_priv *emp = NULL;
	struct ata_link *link = NULL;
	unsigned long flags;

	if (NULL == ap) {
		goto ERROR;
	}

	// del old timer
	pp = ap->private_data;
	spin_lock_irqsave(ap->lock, flags);
	ata_for_each_link(link, ap, EDGE) {
		emp = &pp->em_priv[link->pmp];
		emp->saved_activity = emp->activity = 0;
		del_timer(&emp->timer);
	}

	if (iValue) {
		ap->flags |= ATA_FLAG_SW_ACTIVITY;
		ata_for_each_link(link, ap, EDGE) {
			ahci_init_sw_activity(link);
		}
	} else {
		ap->flags &= ~ATA_FLAG_SW_ACTIVITY;
		ata_for_each_link(link, ap, EDGE) {
			link->flags &= ~ATA_LFLAG_SW_ACTIVITY;
		}
	}
	spin_unlock_irqrestore(ap->lock, flags);
	ret = 0;

ERROR:
	return ret;
}

#ifdef MY_ABC_HERE
// For port_mapping_v2, please use syno_ahci_disk_led_enable_by_port instead of syno_ahci_disk_led_enable
#else /* MY_ABC_HERE */
/**
 * This function is used for AHCI software activity led,
 *
 * hostnum is scsi_host index
 */
int syno_ahci_disk_led_enable(const unsigned short hostnum, const int iValue)
{
	struct Scsi_Host *shost = scsi_host_lookup(hostnum);
	struct ata_port *ap = NULL;
	int ret = -EINVAL;

	if (NULL == shost) {
		goto END;
	}

	if (NULL == (ap = ata_shost_to_port(shost))) {
		goto END;
	}

	ret = __syno_ahci_disk_led_enable(ap, iValue);

END:
	if (shost) {
		scsi_host_put(shost);
	}
	return ret;
}
EXPORT_SYMBOL(syno_ahci_disk_led_enable);
#endif /* MY_ABC_HERE */

#if defined(MY_DEF_HERE) || defined(MY_ABC_HERE)
/**
 * This function is used for AHCI software activity led by disk port.
 * Disk port would be remapped to scsi host number.
 *
 * @param diskPort [IN] is disk port index
 * @param iValue   [IN] is the value going to set.
 */
int syno_ahci_disk_led_enable_by_port(const unsigned short diskPort, const int iValue)
{
#ifdef MY_ABC_HERE
	struct ata_port *ap;
	ap = syno_ata_port_get_by_port(diskPort);
	return __syno_ahci_disk_led_enable(ap, iValue);
#else /* MY_ABC_HERE */
	int i = 0;
	unsigned short scsiHostNum = 0;
	// Try if we can find remap between disk port and scsi host number.
	for (i = 0; i < SATA_REMAP_MAX; i++) {
		if ((unsigned short)syno_get_remap_idx(i) == (diskPort - 1)) {
			scsiHostNum = (unsigned short) i;
			break;
		}
	}

	return syno_ahci_disk_led_enable(scsiHostNum, iValue);
#endif /* MY_ABC_HERE */
}
EXPORT_SYMBOL(syno_ahci_disk_led_enable_by_port);
#endif /* MY_DEF_HERE || MY_ABC_HERE */
#endif /* MY_ABC_HERE || MY_DEF_HERE */

static void ahci_start_port(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_link *link;
	struct ahci_em_priv *emp;
	ssize_t rc;
	int i;

	/* enable FIS reception */
	ahci_start_fis_rx(ap);

	/* enable DMA */
	if (!(hpriv->flags & AHCI_HFLAG_DELAY_ENGINE))
		hpriv->start_engine(ap);

	/* turn on LEDs */
	if (ap->flags & ATA_FLAG_EM) {
		ata_for_each_link(link, ap, EDGE) {
			emp = &pp->em_priv[link->pmp];

			/* EM Transmit bit maybe busy during init */
			for (i = 0; i < EM_MAX_RETRY; i++) {
				rc = ap->ops->transmit_led_message(ap,
							       emp->led_state,
							       4);
				/*
				 * If busy, give a breather but do not
				 * release EH ownership by using msleep()
				 * instead of ata_msleep().  EM Transmit
				 * bit is busy for the whole host and
				 * releasing ownership will cause other
				 * ports to fail the same way.
				 */
				if (rc == -EBUSY)
					msleep(1);
				else
					break;
			}
		}
	}

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
	if (syno_need_ahci_software_activity(ap)) {
		ap->flags |= ATA_FLAG_SW_ACTIVITY;
	}
#endif /* MY_ABC_HERE */

	if (ap->flags & ATA_FLAG_SW_ACTIVITY)
		ata_for_each_link(link, ap, EDGE)
			ahci_init_sw_activity(link);

}

static int ahci_deinit_port(struct ata_port *ap, const char **emsg)
{
	int rc;

	/* disable DMA */
	rc = ahci_stop_engine(ap);
	if (rc) {
		*emsg = "failed to stop engine";
		return rc;
	}

	/* disable FIS reception */
	rc = ahci_stop_fis_rx(ap);
	if (rc) {
		*emsg = "failed stop FIS RX";
		return rc;
	}

	return 0;
}

int ahci_reset_controller(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 tmp;

	/* we must be in AHCI mode, before using anything
	 * AHCI-specific, such as HOST_RESET.
	 */
	ahci_enable_ahci(mmio);

	/* global controller reset */
	if (!ahci_skip_host_reset) {
		tmp = readl(mmio + HOST_CTL);
		if ((tmp & HOST_RESET) == 0) {
			writel(tmp | HOST_RESET, mmio + HOST_CTL);
#ifdef MY_ABC_HERE
			udelay(1);
#endif /* MY_ABC_HERE */
			readl(mmio + HOST_CTL); /* flush */
		}

		/*
		 * to perform host reset, OS should set HOST_RESET
		 * and poll until this bit is read to be "0".
		 * reset must complete within 1 second, or
		 * the hardware should be considered fried.
		 */
		tmp = ata_wait_register(NULL, mmio + HOST_CTL, HOST_RESET,
					HOST_RESET, 10, 1000);

		if (tmp & HOST_RESET) {
			dev_err(host->dev, "controller reset failed (0x%x)\n",
				tmp);
			return -EIO;
		}

		/* turn on AHCI mode */
		ahci_enable_ahci(mmio);

		/* Some registers might be cleared on reset.  Restore
		 * initial values.
		 */
		ahci_restore_initial_config(host);
	} else
		dev_info(host->dev, "skipping global host reset\n");

	return 0;
}
EXPORT_SYMBOL_GPL(ahci_reset_controller);

static void ahci_sw_activity(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];

	if (!(link->flags & ATA_LFLAG_SW_ACTIVITY))
		return;

	emp->activity++;
	if (!timer_pending(&emp->timer))
		mod_timer(&emp->timer, jiffies + msecs_to_jiffies(10));
}

#ifdef MY_DEF_HERE
static void ahci_sw_locate_set(struct ata_link *link, u8 blEnable)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];
	unsigned long led_message = emp->led_state;
	unsigned long flags;

	led_message &= EM_MSG_LED_VALUE;

	led_message |= ap->port_no | (link->pmp << 8);

	emp->locate= blEnable;

	spin_lock_irqsave(ap->lock, flags);
	if (emp->saved_locate == emp->locate) {
		spin_unlock_irqrestore(ap->lock, flags);
		goto END;
	}
	emp->saved_locate = emp->locate;
	led_message &= ~(EM_MSG_LOCATE_LED_MASK);
	led_message |= (emp->locate << 19);
	spin_unlock_irqrestore(ap->lock, flags);
	ahci_transmit_led_message(ap, led_message, 4);
	mdelay(10);
END:
	return;
}

static void ahci_sw_fault_set(struct ata_link *link, u8 blEnable)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];
	unsigned long led_message = emp->led_state;
	unsigned long flags;

	led_message &= EM_MSG_LED_VALUE;

	led_message |= ap->port_no | (link->pmp << 8);

	emp->fault= blEnable;

	spin_lock_irqsave(ap->lock, flags);
	if (emp->saved_fault == emp->fault) {
		spin_unlock_irqrestore(ap->lock, flags);
		goto END;
	}
	emp->saved_fault = emp->fault;
	led_message &= ~(EM_MSG_FAULT_LED_MASK);
	led_message |= (emp->fault << 22);
	spin_unlock_irqrestore(ap->lock, flags);
	ahci_transmit_led_message(ap, led_message, 4);
	mdelay(10);
END:
	return;
}
#endif /* MY_DEF_HERE */

static void ahci_sw_activity_blink(unsigned long arg)
{
	struct ata_link *link = (struct ata_link *)arg;
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];
	unsigned long led_message = emp->led_state;
#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
	struct Scsi_Host *shost;
	struct scsi_device *sdev;
#else
	u32 activity_led_state;
#endif /* MY_ABC_HERE */
	unsigned long flags;

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
	shost = scsi_host_get(ap->scsi_host);

	if (NULL == ap || NULL == shost) {
		goto DO_NOTHING;
	}
	list_for_each_entry(sdev, &shost->__devices, siblings) {
		if ('\0' != sdev->syno_disk_name[0]) {
			goto DO_BLINK;
		}
	}
	goto DO_NOTHING;

DO_BLINK:
#endif /* MY_ABC_HERE || MY_DEF_HERE */

	led_message &= EM_MSG_LED_VALUE;
	led_message |= ap->port_no | (link->pmp << 8);

	/* check to see if we've had activity.  If so,
	 * toggle state of LED and reset timer.  If not,
	 * turn LED to desired idle state.
	 */
	spin_lock_irqsave(ap->lock, flags);
	if (emp->saved_activity != emp->activity) {
		emp->saved_activity = emp->activity;
#if defined(MY_DEF_HERE)
#ifdef MY_ABC_HERE
		syno_set_blink(ap, SYNO_LED_BLINK_ON);
#else /* MY_ABC_HERE */
		SYNO_CTRL_GPIO_HDD_ACT_LED(ap->port_no, 0);
#endif /* MY_ABC_HERE */
#elif defined(MY_ABC_HERE)
		syno_sw_activity(ap);
#else
		/* get the current LED state */
		activity_led_state = led_message & EM_MSG_LED_VALUE_ON;

		if (activity_led_state)
			activity_led_state = 0;
		else
			activity_led_state = 1;

		/* clear old state */
		led_message &= ~EM_MSG_LED_VALUE_ACTIVITY;

		/* toggle state */
		led_message |= (activity_led_state << 16);
#endif /* MY_ABC_HERE */
		mod_timer(&emp->timer, jiffies + msecs_to_jiffies(100));
#ifdef MY_ABC_HERE
#else
	} else {
#if defined(MY_DEF_HERE)
#ifdef MY_ABC_HERE
		syno_set_blink(ap, SYNO_LED_BLINK_OFF);
#else /* MY_ABC_HERE */
		SYNO_CTRL_GPIO_HDD_ACT_LED(ap->port_no, 1);
#endif /* MY_ABC_HERE */
#else /* MY_DEF_HERE */
		/* switch to idle */
		led_message &= ~EM_MSG_LED_VALUE_ACTIVITY;
		if (emp->blink_policy == BLINK_OFF)
			led_message |= (1 << 16);
#endif /* MY_DEF_HERE */
#endif /* MY_ABC_HERE */
	}
	spin_unlock_irqrestore(ap->lock, flags);
#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
DO_NOTHING:
	return;
#else
	ap->ops->transmit_led_message(ap, led_message, 4);
#endif /* MY_ABC_HERE || MY_DEF_HERE */
}

static void ahci_init_sw_activity(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];

	/* init activity stats, setup timer */
	emp->saved_activity = emp->activity = 0;
	setup_timer(&emp->timer, ahci_sw_activity_blink, (unsigned long)link);

#if defined(CONFIG_ARCH_RTD129X) && defined(MY_DEF_HERE) || \
	defined(MY_DEF_HERE)
	emp->blink_policy = BLINK_ON;
#endif /* CONFIG_ARCH_RTD129X && MY_DEF_HERE ||
		  MY_DEF_HERE */

	/* check our blink policy and set flag for link if it's enabled */
#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
#else
	if (emp->blink_policy)
#endif /* MY_ABC_HERE || MY_DEF_HERE */
		link->flags |= ATA_LFLAG_SW_ACTIVITY;
}

int ahci_reset_em(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 em_ctl;

	em_ctl = readl(mmio + HOST_EM_CTL);
	if ((em_ctl & EM_CTL_TM) || (em_ctl & EM_CTL_RST))
		return -EINVAL;

	writel(em_ctl | EM_CTL_RST, mmio + HOST_EM_CTL);
	return 0;
}
EXPORT_SYMBOL_GPL(ahci_reset_em);

#ifdef MY_DEF_HERE
static ssize_t ahci_syno_present_transmit_led_message(struct ata_port *ap, u32 state,
					     ssize_t size)
{
	int iSynoDiskIdx = -1;
	unsigned char uFailLedOn = 0;
	struct ahci_port_priv *pp = ap->private_data;
	unsigned long flags;
	int pmp;
	struct ahci_em_priv *emp;
	ssize_t ret = size;

	pmp = (state & EM_MSG_LED_PMP_SLOT) >> 8;
	if (pmp < EM_MAX_SLOTS) {
		emp = &pp->em_priv[pmp];
	} else {
		ret = -EINVAL;
		goto Err;
	}

	iSynoDiskIdx = syno_libata_index_get(ap->scsi_host, SATA_PMP_MAX_PORTS, 0, 0) + 1;
	if (0 == iSynoDiskIdx) {
		printk(KERN_ERR "%s: Get disk index from ata port error.\n", __func__);
		ret = -ENODEV;
		goto Err;
	}

	if (!HAVE_HDD_PRESENT_LED(iSynoDiskIdx)) {
		printk(KERN_ERR "%s: No such HDD present led pin on disk %u\n", __func__, iSynoDiskIdx);
		ret = -ENODEV;
		goto Err;
	}

	/*
	 * disable green led when orange led on
	 * (POLARITY == 0 -> active high)
	 */
	if (HAVE_HDD_FAIL_LED(iSynoDiskIdx) &&
		(HDD_FAIL_LED_POLARITY(iSynoDiskIdx) ^ SYNO_GPIO_READ(HDD_FAIL_LED_PIN(iSynoDiskIdx)))) {
		uFailLedOn = 1;
	}

	/*
	 * EM_MSG_LED_VALUE_ON will unset when idle,
	 * but we need present led turn on when that time.
	 * So we let present led turn off when EM_MSG_LED_VALUE_ON set.
	 *
	 * SYNO_GPIO_WRITE can't contain gpio_free or other might sleep function.
	 */
	if (state & EM_MSG_LED_VALUE_ON || uFailLedOn) { // turn off green led
		SYNO_GPIO_WRITE(HDD_PRESENT_LED_PIN(iSynoDiskIdx), HDD_PRESENT_LED_POLARITY(iSynoDiskIdx));
	} else { // turn on green led
		SYNO_GPIO_WRITE(HDD_PRESENT_LED_PIN(iSynoDiskIdx), !HDD_PRESENT_LED_POLARITY(iSynoDiskIdx));
	}

	spin_lock_irqsave(ap->lock, flags);
	emp->led_state = state;
	spin_unlock_irqrestore(ap->lock, flags);
Err:
	return ret;
}
#else /* MY_DEF_HERE */
static ssize_t ahci_transmit_led_message(struct ata_port *ap, u32 state,
					ssize_t size)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 em_ctl;
	u32 message[] = {0, 0};
	unsigned long flags;
	int pmp;
	struct ahci_em_priv *emp;

	/* get the slot number from the message */
	pmp = (state & EM_MSG_LED_PMP_SLOT) >> 8;
	if (pmp < EM_MAX_SLOTS)
		emp = &pp->em_priv[pmp];
	else
		return -EINVAL;

	spin_lock_irqsave(ap->lock, flags);

	/*
	 * if we are still busy transmitting a previous message,
	 * do not allow
	 */
	em_ctl = readl(mmio + HOST_EM_CTL);
	if (em_ctl & EM_CTL_TM) {
		spin_unlock_irqrestore(ap->lock, flags);
		return -EBUSY;
	}

	if (hpriv->em_msg_type & EM_MSG_TYPE_LED) {
		/*
		 * create message header - this is all zero except for
		 * the message size, which is 4 bytes.
		 */
		message[0] |= (4 << 8);

		/* ignore 0:4 of byte zero, fill in port info yourself */
		message[1] = ((state & ~EM_MSG_LED_HBA_PORT) | ap->port_no);

		/* write message to EM_LOC */
		writel(message[0], mmio + hpriv->em_loc);
		writel(message[1], mmio + hpriv->em_loc+4);

		/*
		 * tell hardware to transmit the message
		 */
		writel(em_ctl | EM_CTL_TM, mmio + HOST_EM_CTL);
	}

	/* save off new led state for port/slot */
	emp->led_state = state;

	spin_unlock_irqrestore(ap->lock, flags);
	return size;
}
#endif /* MY_DEF_HERE */

static ssize_t ahci_led_show(struct ata_port *ap, char *buf)
{
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_link *link;
	struct ahci_em_priv *emp;
	int rc = 0;

	ata_for_each_link(link, ap, EDGE) {
		emp = &pp->em_priv[link->pmp];
		rc += sprintf(buf, "%lx\n", emp->led_state);
	}
	return rc;
}

static ssize_t ahci_led_store(struct ata_port *ap, const char *buf,
				size_t size)
{
	unsigned int state;
	int pmp;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp;

	if (kstrtouint(buf, 0, &state) < 0)
		return -EINVAL;

	/* get the slot number from the message */
	pmp = (state & EM_MSG_LED_PMP_SLOT) >> 8;
	if (pmp < EM_MAX_SLOTS)
		emp = &pp->em_priv[pmp];
	else
		return -EINVAL;

	/* mask off the activity bits if we are in sw_activity
	 * mode, user should turn off sw_activity before setting
	 * activity led through em_message
	 */
	if (emp->blink_policy)
		state &= ~EM_MSG_LED_VALUE_ACTIVITY;

	return ap->ops->transmit_led_message(ap, state, size);
}

static ssize_t ahci_activity_store(struct ata_device *dev, enum sw_activity val)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];
	u32 port_led_state = emp->led_state;

	/* save the desired Activity LED behavior */
	if (val == OFF) {
		/* clear LFLAG */
		link->flags &= ~(ATA_LFLAG_SW_ACTIVITY);

		/* set the LED to OFF */
		port_led_state &= EM_MSG_LED_VALUE_OFF;
		port_led_state |= (ap->port_no | (link->pmp << 8));
		ap->ops->transmit_led_message(ap, port_led_state, 4);
	} else {
		link->flags |= ATA_LFLAG_SW_ACTIVITY;
		if (val == BLINK_OFF) {
			/* set LED to ON for idle */
			port_led_state &= EM_MSG_LED_VALUE_OFF;
			port_led_state |= (ap->port_no | (link->pmp << 8));
			port_led_state |= EM_MSG_LED_VALUE_ON; /* check this */
			ap->ops->transmit_led_message(ap, port_led_state, 4);
		}
	}
	emp->blink_policy = val;
	return 0;
}

static ssize_t ahci_activity_show(struct ata_device *dev, char *buf)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_em_priv *emp = &pp->em_priv[link->pmp];

	/* display the saved value of activity behavior for this
	 * disk.
	 */
	return sprintf(buf, "%d\n", emp->blink_policy);
}

#if defined(MY_DEF_HERE)
int syno_ahci_disk_green_led(const unsigned short hostnum, const int iValue)
{
	int ret = -EINVAL;

	struct Scsi_Host *shost = scsi_host_lookup(hostnum);
	struct ata_port *ap = NULL;
	struct ahci_port_priv *pp = NULL;
	struct ahci_em_priv *emp = NULL;
	struct ata_link *link = NULL;
	u32 port_led_state = 0;
	unsigned long flags;

	if (NULL == shost) {
		goto END;
	}

	if (NULL == (ap = ata_shost_to_port(shost))) {
		goto END;
	}

	pp = ap->private_data;
	spin_lock_irqsave(ap->lock, flags);
	ata_for_each_link(link, ap, EDGE) {
		emp = &pp->em_priv[link->pmp];
		port_led_state = emp->led_state;

		// clear timer to disable polling ahci_sw_activity_blink
		emp->saved_activity = emp->activity = 0;
		del_timer(&emp->timer);

		if (!iValue) {
			link->flags &= ~(ATA_LFLAG_SW_ACTIVITY);

			// rtd129x need set EM_MSG_LED_VALUE_ON to turn off led
			port_led_state |= EM_MSG_LED_VALUE_ON;
			port_led_state |= (ap->port_no | (link->pmp << 8));
		} else {
			port_led_state &= ~EM_MSG_LED_VALUE_ON;
			port_led_state |= (ap->port_no | (link->pmp << 8));
			ahci_init_sw_activity(link);
		}
	}
	spin_unlock_irqrestore(ap->lock, flags);

	ap->ops->transmit_led_message(ap, port_led_state, 4);

	ret = 0;

END:
	return ret;
}
EXPORT_SYMBOL(syno_ahci_disk_green_led);
#endif /* MY_DEF_HERE */

static void ahci_port_init(struct device *dev, struct ata_port *ap,
			   int port_no, void __iomem *mmio,
			   void __iomem *port_mmio)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	const char *emsg = NULL;
	int rc;
	u32 tmp;
#ifdef MY_DEF_HERE
	struct pci_dev *pdev = NULL;
#endif /* MY_DEF_HERE */

	/* make sure port is not active */
	rc = ahci_deinit_port(ap, &emsg);
	if (rc)
		dev_warn(dev, "%s (%d)\n", emsg, rc);

#if defined(MY_DEF_HERE)
	if (hpriv->comreset_u) {
		u32 reg;

		/* Modify COMRESET spacing upper limit which controls the high
		 * limit of the spacing between two bursts of COMRESET where we
		 * still respond to COMRESET command.
		 *
		 * This is indirect access, so we write the required address,
		 * then read the register, modify it and write back.
		 */
		writel(PORT_OOB_INDIRECT_ADDR, port_mmio + PORT_INDIRECT_ADDR);
		reg = readl(port_mmio + PORT_INDIRECT_DATA);
		reg &= ~PORT_OOB_COMRESET_U_MASK;
		reg |= hpriv->comreset_u;
		writel(reg, port_mmio + PORT_INDIRECT_DATA);
	}

	if (hpriv->comwake) {
		u32 reg;

		/* Modify COMWAKE spacing upper limit which controls the high
		 * limit of the spacing between two bursts of COMWAKE where we
		 * still respond to COMWAKE command.
		 *
		 * This is indirect access, so we write the required address,
		 * then read the register, modify it and write back.
		 */
		writel(PORT_OOB_INDIRECT_ADDR, port_mmio + PORT_INDIRECT_ADDR);
		reg = readl(port_mmio + PORT_INDIRECT_DATA);
		reg &= ~PORT_OOB_COMWAKE_MASK;
		reg |= hpriv->comwake << PORT_OOB_COMWAKE_OFFSET;
		writel(reg, port_mmio + PORT_INDIRECT_DATA);
	}

#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
	if (dev->bus && !strcmp("pci", dev->bus->name)) {
		pdev = to_pci_dev(dev);
		if (pdev->vendor == 0x1b4b && pdev->device == 0x9215) {
			syno_ata_qc_complete_multiple = ata_qc_complete_multiple_delay;
		}
	}

	if (!syno_ata_qc_complete_multiple) {
		syno_ata_qc_complete_multiple = ata_qc_complete_multiple;
	}
#endif /* MY_DEF_HERE */

	/* clear SError */
	tmp = readl(port_mmio + PORT_SCR_ERR);
	VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
	writel(tmp, port_mmio + PORT_SCR_ERR);

	/* clear port IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
	if (tmp)
		writel(tmp, port_mmio + PORT_IRQ_STAT);

	writel(1 << port_no, mmio + HOST_IRQ_STAT);

	/* mark esata ports */
	tmp = readl(port_mmio + PORT_CMD);
	if ((tmp & PORT_CMD_ESP) && (hpriv->cap & HOST_CAP_SXS))
		ap->pflags |= ATA_PFLAG_EXTERNAL;
}

#ifdef MY_DEF_HERE
#ifdef MY_ABC_HERE
bool syno_hard_irq_check(void)
{
	const char *ahci_irq_type;
	bool blRet = false;

	if (of_property_read_string(of_root, SZ_DTS_AHCI_IRQ, &ahci_irq_type)) {
		goto END;
	}
	if (0 == strcmp(ahci_irq_type, SZ_AHCI_HARD_IRQ)) {
		blRet = true;
	}

END:
	return blRet;
}
#else /* MY_ABC_HERE */
extern bool gSynoAtaAhciHardIrq;
bool syno_hard_irq_check(void)
{
	return gSynoAtaAhciHardIrq;
}
#endif /* MY_ABC_HERE */
#endif /* MY_DEF_HERE */

void ahci_init_controller(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	int i;
	void __iomem *port_mmio;
	u32 tmp;
#ifdef MY_ABC_HERE
	struct pci_dev *pdev = NULL;
#endif /* MY_ABC_HERE */

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		port_mmio = ahci_port_base(ap);
		if (ata_port_is_dummy(ap))
			continue;

		ahci_port_init(host->dev, ap, i, mmio, port_mmio);
	}
#ifdef MY_ABC_HERE
	pdev = to_pci_dev(host->dev);
	if (0 == syno_jmb58x_check(pdev->vendor, pdev->device)) {
#ifdef MY_DEF_HERE
		if (syno_hard_irq_check()) {
			syno_ahci_multi_irqs_intr = &syno_ahci_multi_hardirqs_intr_jmb;
			syno_ahci_port_thread_fn = NULL;
		} else {
			syno_ahci_multi_irqs_intr = &syno_ahci_multi_irqs_intr_jmb;
			syno_ahci_port_thread_fn = &ahci_port_thread_fn;
		}
#else /* MY_DEF_HERE */
		syno_ahci_multi_irqs_intr = &syno_ahci_multi_irqs_intr_jmb;
#endif /* MY_DEF_HERE */
	} else {
		syno_ahci_multi_irqs_intr = &ahci_multi_irqs_intr;
#ifdef MY_DEF_HERE
		syno_ahci_port_thread_fn = &ahci_port_thread_fn;
#endif /* MY_DEF_HERE */
	}
#endif /* MY_ABC_HERE */

	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
	writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL);
	tmp = readl(mmio + HOST_CTL);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
}
EXPORT_SYMBOL_GPL(ahci_init_controller);

static void ahci_dev_config(struct ata_device *dev)
{
	struct ahci_host_priv *hpriv = dev->link->ap->host->private_data;

	if (hpriv->flags & AHCI_HFLAG_SECT255) {
		dev->max_sectors = 255;
		ata_dev_info(dev,
			     "SB600 AHCI: limiting to 255 sectors per cmd\n");
	}
}

unsigned int ahci_dev_classify(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_taskfile tf;
	u32 tmp;

	tmp = readl(port_mmio + PORT_SIG);
	tf.lbah		= (tmp >> 24)	& 0xff;
	tf.lbam		= (tmp >> 16)	& 0xff;
	tf.lbal		= (tmp >> 8)	& 0xff;
	tf.nsect	= (tmp)		& 0xff;

	return ata_dev_classify(&tf);
}
EXPORT_SYMBOL_GPL(ahci_dev_classify);

void ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			u32 opts)
{
	dma_addr_t cmd_tbl_dma;

	cmd_tbl_dma = pp->cmd_tbl_dma + tag * AHCI_CMD_TBL_SZ;

	pp->cmd_slot[tag].opts = cpu_to_le32(opts);
	pp->cmd_slot[tag].status = 0;
	pp->cmd_slot[tag].tbl_addr = cpu_to_le32(cmd_tbl_dma & 0xffffffff);
	pp->cmd_slot[tag].tbl_addr_hi = cpu_to_le32((cmd_tbl_dma >> 16) >> 16);
}
EXPORT_SYMBOL_GPL(ahci_fill_cmd_slot);

int ahci_kick_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u8 status = readl(port_mmio + PORT_TFDATA) & 0xFF;
	u32 tmp;
	int busy, rc;

	/* stop engine */
	rc = ahci_stop_engine(ap);
	if (rc)
		goto out_restart;

	/* need to do CLO?
	 * always do CLO if PMP is attached (AHCI-1.3 9.2)
	 */
	busy = status & (ATA_BUSY | ATA_DRQ);
	if (!busy && !sata_pmp_attached(ap)) {
		rc = 0;
		goto out_restart;
	}

	if (!(hpriv->cap & HOST_CAP_CLO)) {
		rc = -EOPNOTSUPP;
		goto out_restart;
	}

	/* perform CLO */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_CLO;
	writel(tmp, port_mmio + PORT_CMD);

	rc = 0;
	tmp = ata_wait_register(ap, port_mmio + PORT_CMD,
				PORT_CMD_CLO, PORT_CMD_CLO, 1, 500);
	if (tmp & PORT_CMD_CLO)
		rc = -EIO;

	/* restart engine */
 out_restart:
	hpriv->start_engine(ap);
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_kick_engine);

static int ahci_exec_polled_cmd(struct ata_port *ap, int pmp,
				struct ata_taskfile *tf, int is_cmd, u16 flags,
				unsigned long timeout_msec)
{
	const u32 cmd_fis_len = 5; /* five dwords */
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u8 *fis = pp->cmd_tbl;
	u32 tmp;

	/* prep the command */
	ata_tf_to_fis(tf, pmp, is_cmd, fis);
	ahci_fill_cmd_slot(pp, 0, cmd_fis_len | flags | (pmp << 12));

	/* set port value for softreset of Port Multiplier */
	if (pp->fbs_enabled && pp->fbs_last_dev != pmp) {
		tmp = readl(port_mmio + PORT_FBS);
		tmp &= ~(PORT_FBS_DEV_MASK | PORT_FBS_DEC);
		tmp |= pmp << PORT_FBS_DEV_OFFSET;
		writel(tmp, port_mmio + PORT_FBS);
		pp->fbs_last_dev = pmp;
	}

	/* issue & wait */
	writel(1, port_mmio + PORT_CMD_ISSUE);

	if (timeout_msec) {
		tmp = ata_wait_register(ap, port_mmio + PORT_CMD_ISSUE,
					0x1, 0x1, 1, timeout_msec);
		if (tmp & 0x1) {
			ahci_kick_engine(ap);
			return -EBUSY;
		}
	} else
		readl(port_mmio + PORT_CMD_ISSUE);	/* flush */

	return 0;
}

int ahci_do_softreset(struct ata_link *link, unsigned int *class,
		      int pmp, unsigned long deadline,
		      int (*check_ready)(struct ata_link *link))
{
	struct ata_port *ap = link->ap;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	const char *reason = NULL;
	unsigned long now, msecs;
	struct ata_taskfile tf;
	bool fbs_disabled = false;
	int rc;

	DPRINTK("ENTER\n");

	/* prepare for SRST (AHCI-1.1 10.4.1) */
	rc = ahci_kick_engine(ap);
	if (rc && rc != -EOPNOTSUPP)
		ata_link_warn(link, "failed to reset engine (errno=%d)\n", rc);

	/*
	 * According to AHCI-1.2 9.3.9: if FBS is enable, software shall
	 * clear PxFBS.EN to '0' prior to issuing software reset to devices
	 * that is attached to port multiplier.
	 */
	if (!ata_is_host_link(link) && pp->fbs_enabled) {
		ahci_disable_fbs(ap);
		fbs_disabled = true;
	}

	ata_tf_init(link->device, &tf);

	/* issue the first D2H Register FIS */
	msecs = 0;
	now = jiffies;
	if (time_after(deadline, now))
		msecs = jiffies_to_msecs(deadline - now);

	tf.ctl |= ATA_SRST;
	if (ahci_exec_polled_cmd(ap, pmp, &tf, 0,
				 AHCI_CMD_RESET | AHCI_CMD_CLR_BUSY, msecs)) {
		rc = -EIO;
		reason = "1st FIS failed";
		goto fail;
	}

	/* spec says at least 5us, but be generous and sleep for 1ms */
	ata_msleep(ap, 1);

	/* issue the second D2H Register FIS */
	tf.ctl &= ~ATA_SRST;
#ifdef MY_ABC_HERE
	if ((hpriv->flags & AHCI_HFLAG_YES_MV9235_FIX)) {
		/* 9235 may fail at 2nd D2H, so we use the same check as 1st D2H */
		msecs = 0;
		now = jiffies;
		if (time_after(deadline, now))
			msecs = jiffies_to_msecs(deadline - now);
		if(ahci_exec_polled_cmd(ap, pmp, &tf, 0, 0, msecs)) {
			rc = -EIO;
			reason = "2nd FIS failed";
			goto fail;
		}
	} else {
#endif /* MY_ABC_HERE */
	ahci_exec_polled_cmd(ap, pmp, &tf, 0, 0, 0);
#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */

	/* wait for link to become ready */
	rc = ata_wait_after_reset(link, deadline, check_ready);
	if (rc == -EBUSY && hpriv->flags & AHCI_HFLAG_SRST_TOUT_IS_OFFLINE) {
		/*
		 * Workaround for cases where link online status can't
		 * be trusted.  Treat device readiness timeout as link
		 * offline.
		 */
		ata_link_info(link, "device not ready, treating as offline\n");
		*class = ATA_DEV_NONE;
	} else if (rc) {
		/* link occupied, -ENODEV too is an error */
		reason = "device not ready";
		goto fail;
	} else
		*class = ahci_dev_classify(ap);

	/* re-enable FBS if disabled before */
	if (fbs_disabled)
		ahci_enable_fbs(ap);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 fail:
	ata_link_err(link, "softreset failed (%s)\n", reason);
#ifdef MY_ABC_HERE
	if (-EBUSY == rc) {
		ata_link_printk(link, KERN_ERR, "SRST fail, set srst fail flag\n");
		link->uiSflags |= ATA_SYNO_FLAG_SRST_FAIL;
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	/* re-enable FBS if disabled before */
	if (fbs_disabled)
		ahci_enable_fbs(ap);
#endif /* MY_ABC_HERE */
	return rc;
}

int ahci_check_ready(struct ata_link *link)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	u8 status = readl(port_mmio + PORT_TFDATA) & 0xFF;

	return ata_check_ready(status);
}
EXPORT_SYMBOL_GPL(ahci_check_ready);

static int ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	int pmp = sata_srst_pmp(link);

	DPRINTK("ENTER\n");

	return ahci_do_softreset(link, class, pmp, deadline, ahci_check_ready);
}
EXPORT_SYMBOL_GPL(ahci_do_softreset);

static int ahci_bad_pmp_check_ready(struct ata_link *link)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	u8 status = readl(port_mmio + PORT_TFDATA) & 0xFF;
	u32 irq_status = readl(port_mmio + PORT_IRQ_STAT);

	/*
	 * There is no need to check TFDATA if BAD PMP is found due to HW bug,
	 * which can save timeout delay.
	 */
	if (irq_status & PORT_IRQ_BAD_PMP)
		return -EIO;

	return ata_check_ready(status);
}

static int ahci_pmp_retry_softreset(struct ata_link *link, unsigned int *class,
				    unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	int pmp = sata_srst_pmp(link);
	int rc;
	u32 irq_sts;

	DPRINTK("ENTER\n");

	rc = ahci_do_softreset(link, class, pmp, deadline,
			       ahci_bad_pmp_check_ready);

	/*
	 * Soft reset fails with IPMS set when PMP is enabled but
	 * SATA HDD/ODD is connected to SATA port, do soft reset
	 * again to port 0.
	 */
	if (rc == -EIO) {
		irq_sts = readl(port_mmio + PORT_IRQ_STAT);
		if (irq_sts & PORT_IRQ_BAD_PMP) {
			ata_link_warn(link,
					"applying PMP SRST workaround "
					"and retrying\n");
			rc = ahci_do_softreset(link, class, 0, deadline,
					       ahci_check_ready);
		}
	}

	return rc;
}

static int ahci_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	bool online;
	int rc;

	DPRINTK("ENTER\n");

	ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = ATA_BUSY;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);

	rc = sata_link_hardreset(link, timing, deadline, &online,
				 ahci_check_ready);

	hpriv->start_engine(ap);

	if (online)
		*class = ahci_dev_classify(ap);

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}

static void ahci_postreset(struct ata_link *link, unsigned int *class)
{
	struct ata_port *ap = link->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 new_tmp, tmp;

	ata_std_postreset(link, class);

	/* Make sure port's ATAPI bit is set appropriately */
	new_tmp = tmp = readl(port_mmio + PORT_CMD);
	if (*class == ATA_DEV_ATAPI)
		new_tmp |= PORT_CMD_ATAPI;
	else
		new_tmp &= ~PORT_CMD_ATAPI;
	if (new_tmp != tmp) {
		writel(new_tmp, port_mmio + PORT_CMD);
		readl(port_mmio + PORT_CMD); /* flush */
	}
}

static unsigned int ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl)
{
	struct scatterlist *sg;
	struct ahci_sg *ahci_sg = cmd_tbl + AHCI_CMD_TBL_HDR_SZ;
	unsigned int si;

	VPRINTK("ENTER\n");

	/*
	 * Next, the S/G list.
	 */
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		dma_addr_t addr = sg_dma_address(sg);
		u32 sg_len = sg_dma_len(sg);

		ahci_sg[si].addr = cpu_to_le32(addr & 0xffffffff);
		ahci_sg[si].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		ahci_sg[si].flags_size = cpu_to_le32(sg_len - 1);
	}

	return si;
}

#ifdef MY_ABC_HERE
int sata_syno_ahci_defer_cmd(struct ata_queued_cmd *qc)
{
	struct ata_link *link = qc->dev->link;
	struct ata_port *ap = link->ap;
	u8 prot = qc->tf.protocol;

	int is_excl = (ata_is_atapi(prot) ||
		       (qc->flags & ATA_QCFLAG_RESULT_TF));

	if (unlikely(ap->excl_link)) {
		if (link == ap->excl_link) {
			if (ap->nr_active_links)
				return ATA_DEFER_PORT;
			qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
		} else {
			if (!ap->nr_active_links) {
				/* Since we are here now, just preempt */
				if (is_excl) {
					ap->excl_link = link;
					qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
				} else {
					/* normal I/O should preempt in this situation */
					ap->excl_link = NULL;
				}
			} else {
				return ATA_DEFER_PORT;
			}
		}
	} else if (unlikely(is_excl)) {
		ap->excl_link = link;
		if (ap->nr_active_links)
			return ATA_DEFER_PORT;
		qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
	}

	return ata_std_qc_defer(qc);
}
EXPORT_SYMBOL_GPL(sata_syno_ahci_defer_cmd);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
int ahci_syno_pmp_3x26_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	if (sata_pmp_attached(ap) && (ap->uiStsFlags & SYNO_STATUS_IS_SIL3x26)) {
		return sata_syno_ahci_defer_cmd(qc);
	}
	else
		return ata_std_qc_defer(qc);
}
EXPORT_SYMBOL_GPL(ahci_syno_pmp_3x26_qc_defer);
#endif /* MY_ABC_HERE */

static int ahci_pmp_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_port_priv *pp = ap->private_data;

	if (!sata_pmp_attached(ap) || pp->fbs_enabled)
		return ata_std_qc_defer(qc);
	else
		return sata_pmp_qc_defer_cmd_switch(qc);
}

static enum ata_completion_errors ahci_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_port_priv *pp = ap->private_data;
	int is_atapi = ata_is_atapi(qc->tf.protocol);
	void *cmd_tbl;
	u32 opts;
	const u32 cmd_fis_len = 5; /* five dwords */
	unsigned int n_elem;

	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	cmd_tbl = pp->cmd_tbl + qc->tag * AHCI_CMD_TBL_SZ;

	ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, cmd_tbl);
	if (is_atapi) {
		memset(cmd_tbl + AHCI_CMD_TBL_CDB, 0, 32);
		memcpy(cmd_tbl + AHCI_CMD_TBL_CDB, qc->cdb, qc->dev->cdb_len);
	}

	n_elem = 0;
	if (qc->flags & ATA_QCFLAG_DMAMAP)
		n_elem = ahci_fill_sg(qc, cmd_tbl);

	/*
	 * Fill in command slot information.
	 */
	opts = cmd_fis_len | n_elem << 16 | (qc->dev->link->pmp << 12);
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;
	if (is_atapi)
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;

	ahci_fill_cmd_slot(pp, qc->tag, opts);

	return AC_ERR_OK;
}

static void ahci_fbs_dec_intr(struct ata_port *ap)
{
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 fbs = readl(port_mmio + PORT_FBS);
	int retries = 3;

	DPRINTK("ENTER\n");
	BUG_ON(!pp->fbs_enabled);

	/* time to wait for DEC is not specified by AHCI spec,
	 * add a retry loop for safety.
	 */
	writel(fbs | PORT_FBS_DEC, port_mmio + PORT_FBS);
	fbs = readl(port_mmio + PORT_FBS);
	while ((fbs & PORT_FBS_DEC) && retries--) {
		udelay(1);
		fbs = readl(port_mmio + PORT_FBS);
	}

	if (fbs & PORT_FBS_DEC)
		dev_err(ap->host->dev, "failed to clear device error\n");
}

#if defined(MY_DEF_HERE)
void ahci_error_intr(struct ata_port *ap, u32 irq_stat)
#else /* MY_DEF_HERE */
static void ahci_error_intr(struct ata_port *ap, u32 irq_stat)
#endif /* MY_DEF_HERE */

{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_eh_info *host_ehi = &ap->link.eh_info;
	struct ata_link *link = NULL;
	struct ata_queued_cmd *active_qc;
	struct ata_eh_info *active_ehi;
	bool fbs_need_dec = false;
	u32 serror;

	/* determine active link with error */
	if (pp->fbs_enabled) {
		void __iomem *port_mmio = ahci_port_base(ap);
		u32 fbs = readl(port_mmio + PORT_FBS);
		int pmp = fbs >> PORT_FBS_DWE_OFFSET;

		if ((fbs & PORT_FBS_SDE) && (pmp < ap->nr_pmp_links)) {
			link = &ap->pmp_link[pmp];
			fbs_need_dec = true;
		}

	} else
		ata_for_each_link(link, ap, EDGE)
			if (ata_link_active(link))
				break;

	if (!link)
		link = &ap->link;

	active_qc = ata_qc_from_tag(ap, link->active_tag);
	active_ehi = &link->eh_info;

	/* record irq stat */
#ifdef MY_ABC_HERE
	// ('\0' == host_ehi->desc[0]) is true when there is no unhandle message is desc
	if ((0 == syno_jmb58x_check(ap->host->vendor, ap->host->device))
	    && (irq_stat & PORT_IRQ_BAD_PMP) && ('\0' != host_ehi->desc[0])) {
		irq_stat &= ~PORT_IRQ_BAD_PMP;
	} else {
#endif /* MY_ABC_HERE */
	ata_ehi_clear_desc(host_ehi);
	ata_ehi_push_desc(host_ehi, "irq_stat 0x%08x", irq_stat);
#ifdef MY_ABC_HERE
	}
#endif /* MY_ABC_HERE */

	/* AHCI needs SError cleared; otherwise, it might lock up */
	ahci_scr_read(&ap->link, SCR_ERROR, &serror);
	ahci_scr_write(&ap->link, SCR_ERROR, serror);
	host_ehi->serror |= serror;

#ifdef MY_ABC_HERE
	/* irq_off case */
	if (ap->pflags & ATA_PFLAG_SYNO_IRQ_OFF) {
		/* Only support deep sleep port, we can on ATA_PFLAG_SYNO_IRQ_OFF.
		 * So if this case happened, we should BUG */
		if (0 == iIsSynoDeepSleepSupport(ap) && !(ap->pflags & ATA_PFLAG_SYNO_DS_PWROFF)) {
			printk("BUG!!! This port %d didn't support deep sleep\n", ap->print_id);
			WARN_ON(1);
			ap->pflags &= ~ATA_PFLAG_SYNO_IRQ_OFF;
			host_ehi->action |= ATA_EH_RESET;
		} else if (irq_stat & (PORT_IRQ_PHYRDY | PORT_IRQ_CONNECT)) {
			/* NOTE the caller must make sure, can on irq_off, so we just WARN_ON here. And still
			 * let this interrupt ignore */
			if (ap->nr_active_links && !(ap->pflags & ATA_PFLAG_SYNO_DS_PWROFF)) {
				printk("WARNING: disk %d irq off but still have cmd. Reset now. irq_stat 0x%x\n",
						ap->print_id, irq_stat);
				host_ehi->action |= ATA_EH_RESET;
			} else {
				DBGMESG("disk %d irq off, ignore this interrupt, irq_stat 0x%x\n", ap->print_id, irq_stat);
				return;
			}
		}else {
			printk("WARNING: disk %d irq off but received un-wanted interrupts, reset now. irq_stat 0x%x\n",
				ap->print_id, irq_stat);
			WARN_ON(1);
			host_ehi->action |= ATA_EH_RESET;
		}
	}
#endif /* MY_ABC_HERE */

	/* some controllers set IRQ_IF_ERR on device errors, ignore it */
	if (hpriv->flags & AHCI_HFLAG_IGN_IRQ_IF_ERR)
		irq_stat &= ~PORT_IRQ_IF_ERR;

	if (irq_stat & PORT_IRQ_TF_ERR) {
		/* If qc is active, charge it; otherwise, the active
		 * link.  There's no active qc on NCQ errors.  It will
		 * be determined by EH by reading log page 10h.
		 */
		if (active_qc)
			active_qc->err_mask |= AC_ERR_DEV;
		else
			active_ehi->err_mask |= AC_ERR_DEV;

		if (hpriv->flags & AHCI_HFLAG_IGN_SERR_INTERNAL)
			host_ehi->serror &= ~SERR_INTERNAL;
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		u32 *unk = pp->rx_fis + RX_FIS_UNK;

		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(active_ehi,
				  "unknown FIS %08x %08x %08x %08x" ,
				  unk[0], unk[1], unk[2], unk[3]);
	}

	if (sata_pmp_attached(ap) && (irq_stat & PORT_IRQ_BAD_PMP)) {
		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(active_ehi, "incorrect PMP");
	}

	if (irq_stat & (PORT_IRQ_HBUS_ERR | PORT_IRQ_HBUS_DATA_ERR)) {
		host_ehi->err_mask |= AC_ERR_HOST_BUS;
		host_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(host_ehi, "host bus error");
	}

	if (irq_stat & PORT_IRQ_IF_ERR) {
		if (fbs_need_dec)
			active_ehi->err_mask |= AC_ERR_DEV;
		else {
			host_ehi->err_mask |= AC_ERR_ATA_BUS;
			host_ehi->action |= ATA_EH_RESET;
		}
#ifdef MY_ABC_HERE
		if (0 != syno_jmb58x_check(ap->host->vendor, ap->host->device)) {
			goto SKIP_JMB585_DUBIOS_IFS_WORKAROUND;
		}
		// if IFS and Proto error exist, we use workaround to check for the real error.
		// The workaround only works for the first attempt.
		if ((PORT_IRQ_IF_ERR & irq_stat) && (SERR_PROTOCOL & host_ehi->serror) && ATA_EH_MAX_TRIES == ap->eh_tries) {
			if (fbs_need_dec) {
				irq_stat &= ~PORT_IRQ_IF_ERR;
				active_ehi->uiJM585DubiosIFSProtoFlag |= ATA_SYNO_FLAG_JM585_READ_LOG;
			} else {
				host_ehi->uiJM585DubiosIFSProtoFlag |= ATA_SYNO_FLAG_JM585_READ_LOG;
			}
		}
SKIP_JMB585_DUBIOS_IFS_WORKAROUND:
#endif /* MY_ABC_HERE */
		ata_ehi_push_desc(host_ehi, "interface fatal error");
	}

#ifdef MY_ABC_HERE
	if ((irq_stat & (PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)) || (ap->uiSflags & ATA_SYNO_FLAG_FORCE_INTR)) {
		if (ap->uiSflags & ATA_SYNO_FLAG_FORCE_INTR) {
			ap->uiSflags &= ~ATA_SYNO_FLAG_FORCE_INTR;
			DBGMESG("ata%u: clear ATA_SYNO_FLAG_FORCE_INTR\n", ap->print_id);
		} else {
			ap->iDetectStat = 1;
			DBGMESG("ata%u: set detect stat check\n", ap->print_id);
		}
#else /* MY_ABC_HERE */
	if (irq_stat & (PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)) {
#endif /* MY_ABC_HERE */
#if defined(CONFIG_AHCI_RTK) && defined(CONFIG_SYNO_LSP_RTD1619)
		if (irq_stat & PORT_IRQ_CONNECT) {
			ap->hotplug_flag = 1;
		} else {
			ap->hotplug_flag = 0;
		}
#endif /* CONFIG_AHCI_RTK && CONFIG_SYNO_LSP_RTD1619 */
#ifdef MY_ABC_HERE
		syno_ata_info_print(ap);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
		if (irq_stat & PORT_IRQ_CONNECT) {
			ap->pflags |= ATA_PFLAG_SYNO_BOOT_PROBE;
		}
#endif /* MY_ABC_HERE */
		ata_ehi_hotplugged(host_ehi);
		ata_ehi_push_desc(host_ehi, "%s",
			irq_stat & PORT_IRQ_CONNECT ?
			"connection status changed" : "PHY RDY changed");
	}

	/* okay, let's hand over to EH */

	if (irq_stat & PORT_IRQ_FREEZE)
		ata_port_freeze(ap);
	else if (fbs_need_dec) {
		ata_link_abort(link);
		ahci_fbs_dec_intr(ap);
	} else
		ata_port_abort(ap);
}
#if defined(MY_DEF_HERE)
EXPORT_SYMBOL_GPL(ahci_error_intr);
#endif /* MY_DEF_HERE */

static void ahci_handle_port_interrupt(struct ata_port *ap,
				       void __iomem *port_mmio, u32 status)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	int resetting = !!(ap->pflags & ATA_PFLAG_RESETTING);
	u32 qc_active = 0;
	int rc;

	/* ignore BAD_PMP while resetting */
	if (unlikely(resetting))
		status &= ~PORT_IRQ_BAD_PMP;

	if (sata_lpm_ignore_phy_events(&ap->link)) {
		status &= ~PORT_IRQ_PHYRDY;
		ahci_scr_write(&ap->link, SCR_ERROR, SERR_PHYRDY_CHG);
	}

#ifdef MY_ABC_HERE
	if (unlikely(status & PORT_IRQ_ERROR) || (ap->uiSflags & ATA_SYNO_FLAG_FORCE_INTR)) {
#else /* MY_ABC_HERE */
	if (unlikely(status & PORT_IRQ_ERROR)) {
#endif /* MY_ABC_HERE */
		ahci_error_intr(ap, status);
		return;
	}

	if (status & PORT_IRQ_SDB_FIS) {
#ifdef MY_ABC_HERE
		if (ap->pflags & ATA_PFLAG_SYNO_IRQ_OFF) {
			/* irq_off case */
			u32 sntf = 0;
			sntf = readl(port_mmio + PORT_SCR_NTF);
			if (sntf & (1<< SATA_PMP_CTRL_PORT)) {
				if (status & (PORT_IRQ_PHYRDY | PORT_IRQ_CONNECT)) {
					/* NOTE the caller must make sure, can on irq_off, so we just WARN_ON here.
					 * The following action is call sata_async_notification()
					 * and it will call EH.
					 */
					printk("WARNING: pmp disk %d irq off but still have cmd. Reset now. irq_stat 0x%x\n",
							ap->print_id, status);
					ap->pflags &= ~ATA_PFLAG_SYNO_IRQ_OFF;
				}
			}
		}
#endif /* MY_ABC_HERE */
		/* If SNotification is available, leave notification
		 * handling to sata_async_notification().  If not,
		 * emulate it by snooping SDB FIS RX area.
		 *
		 * Snooping FIS RX area is probably cheaper than
		 * poking SNotification but some constrollers which
		 * implement SNotification, ICH9 for example, don't
		 * store AN SDB FIS into receive area.
		 */
		if (hpriv->cap & HOST_CAP_SNTF)
			sata_async_notification(ap);
		else {
			/* If the 'N' bit in word 0 of the FIS is set,
			 * we just received asynchronous notification.
			 * Tell libata about it.
			 *
			 * Lack of SNotification should not appear in
			 * ahci 1.2, so the workaround is unnecessary
			 * when FBS is enabled.
			 */
			if (pp->fbs_enabled)
				WARN_ON_ONCE(1);
			else {
				const __le32 *f = pp->rx_fis + RX_FIS_SDB;
				u32 f0 = le32_to_cpu(f[0]);
				if (f0 & (1 << 15))
					sata_async_notification(ap);
			}
		}
	}

	/* pp->active_link is not reliable once FBS is enabled, both
	 * PORT_SCR_ACT and PORT_CMD_ISSUE should be checked because
	 * NCQ and non-NCQ commands may be in flight at the same time.
	 */
	if (pp->fbs_enabled) {
		if (ap->qc_active) {
			qc_active = readl(port_mmio + PORT_SCR_ACT);
			qc_active |= readl(port_mmio + PORT_CMD_ISSUE);
		}
	} else {
		/* pp->active_link is valid iff any command is in flight */
		if (ap->qc_active && pp->active_link->sactive)
			qc_active = readl(port_mmio + PORT_SCR_ACT);
		else
			qc_active = readl(port_mmio + PORT_CMD_ISSUE);
	}


#ifdef MY_DEF_HERE
	rc = syno_ata_qc_complete_multiple(ap, qc_active);
#else /* MY_DEF_HERE */
	rc = ata_qc_complete_multiple(ap, qc_active);
#endif /* MY_DEF_HERE */

	/* while resetting, invalid completions are expected */
	if (unlikely(rc < 0 && !resetting)) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_RESET;
		ata_port_freeze(ap);
	}
}

#ifdef MY_DEF_HERE
static void syno_internal_ahci_handle_port_interrupt(struct ata_port *ap,
				       void __iomem *port_mmio, u32 status)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ahci_port_priv *pp = ap->private_data;
	int resetting = !!(ap->pflags & ATA_PFLAG_RESETTING);
	u32 qc_active = 0;
	int rc;

	/* ignore BAD_PMP while resetting */
	if (unlikely(resetting))
		status &= ~PORT_IRQ_BAD_PMP;

	if (sata_lpm_ignore_phy_events(&ap->link)) {
		status &= ~PORT_IRQ_PHYRDY;
		ahci_scr_write(&ap->link, SCR_ERROR, SERR_PHYRDY_CHG);
	}

#ifdef MY_ABC_HERE
	if (unlikely(status & PORT_IRQ_ERROR) || (ap->uiSflags & ATA_SYNO_FLAG_FORCE_INTR)) {
#else /* MY_ABC_HERE */
	if (unlikely(status & PORT_IRQ_ERROR)) {
#endif /* MY_ABC_HERE */
		ahci_error_intr(ap, status);
		return;
	}

	/* pp->active_link is not reliable once FBS is enabled, both
	 * PORT_SCR_ACT and PORT_CMD_ISSUE should be checked because
	 * NCQ and non-NCQ commands may be in flight at the same time.
	 */
	if (pp->fbs_enabled) {
		if (ap->qc_active) {
			qc_active = readl(port_mmio + PORT_SCR_ACT);
			qc_active |= readl(port_mmio + PORT_CMD_ISSUE);
		}
	} else {
		/* pp->active_link is valid iff any command is in flight */
		if (ap->qc_active && pp->active_link->sactive)
			qc_active = readl(port_mmio + PORT_SCR_ACT);
		else
			qc_active = readl(port_mmio + PORT_CMD_ISSUE);
	}

#ifdef MY_DEF_HERE
	rc = syno_ata_qc_complete_multiple(ap, qc_active);
#else /* MY_DEF_HERE */
	rc = ata_qc_complete_multiple(ap, qc_active);
#endif /* MY_DEF_HERE */

	/* while resetting, invalid completions are expected */
	if (unlikely(rc < 0 && !resetting)) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_RESET;
		ata_port_freeze(ap);
	}
}
#endif /* MY_DEF_HERE */

static void ahci_port_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 status;

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status, port_mmio + PORT_IRQ_STAT);

#ifdef MY_DEF_HERE
	if (likely(ap->syno_ahci_handle_port_interrupt)) {
		ap->syno_ahci_handle_port_interrupt(ap, port_mmio, status);
	}
#else /* MY_DEF_HERE */
	ahci_handle_port_interrupt(ap, port_mmio, status);
#endif /* MY_DEF_HERE */
}

#ifdef MY_DEF_HERE
static irqreturn_t syno_ahci_multi_hardirqs_intr_jmb(int irq, void *dev_instance)
{
	struct ata_port *ap = dev_instance;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 status;
	VPRINTK("ENTER\n");

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status & ~(PORT_IRQ_PHYRDY | PORT_IRQ_CONNECT), port_mmio + PORT_IRQ_STAT);

	atomic_set(&pp->intr_status, 0);
	if (!status)
		return IRQ_NONE;

	spin_lock(ap->lock);
#ifdef MY_DEF_HERE
	if (likely(ap->syno_ahci_handle_port_interrupt)) {
		ap->syno_ahci_handle_port_interrupt(ap, port_mmio, status);
	}
#else /* MY_DEF_HERE */
	ahci_handle_port_interrupt(ap, port_mmio, status);
#endif /* MY_DEF_HERE */
	spin_unlock(ap->lock);
	VPRINTK("EXIT\n");

	return IRQ_HANDLED;
}
#endif /* MY_DEF_HERE */

static irqreturn_t ahci_port_thread_fn(int irq, void *dev_instance)
{
	struct ata_port *ap = dev_instance;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 status;

	status = atomic_xchg(&pp->intr_status, 0);
	if (!status)
		return IRQ_NONE;

	spin_lock_bh(ap->lock);
#ifdef MY_DEF_HERE
	if (likely(ap->syno_ahci_handle_port_interrupt)) {
		ap->syno_ahci_handle_port_interrupt(ap, port_mmio, status);
	}
#else /* MY_DEF_HERE */
	ahci_handle_port_interrupt(ap, port_mmio, status);
#endif /* MY_DEF_HERE */
	spin_unlock_bh(ap->lock);

	return IRQ_HANDLED;
}

#ifdef MY_ABC_HERE
static irqreturn_t syno_ahci_multi_irqs_intr_jmb(int irq, void *dev_instance)
{
	struct ata_port *ap = dev_instance;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 status;
	VPRINTK("ENTER\n");

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status & ~(PORT_IRQ_PHYRDY | PORT_IRQ_CONNECT), port_mmio + PORT_IRQ_STAT);
	atomic_or(status, &pp->intr_status);

	VPRINTK("EXIT\n");

	return IRQ_WAKE_THREAD;
}
#endif /* MY_ABC_HERE */

static irqreturn_t ahci_multi_irqs_intr(int irq, void *dev_instance)
{
	struct ata_port *ap = dev_instance;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 status;

	VPRINTK("ENTER\n");

	status = readl(port_mmio + PORT_IRQ_STAT);
	writel(status, port_mmio + PORT_IRQ_STAT);

	atomic_or(status, &pp->intr_status);

	VPRINTK("EXIT\n");

	return IRQ_WAKE_THREAD;
}

static u32 ahci_handle_port_intr(struct ata_host *host, u32 irq_masked)
{
	unsigned int i, handled = 0;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		if (!(irq_masked & (1 << i)))
			continue;

		ap = host->ports[i];
		if (ap) {
			ahci_port_intr(ap);
			VPRINTK("port %u\n", i);
		} else {
			VPRINTK("port %u (no irq)\n", i);
			if (ata_ratelimit())
				dev_warn(host->dev,
					 "interrupt on disabled port %u\n", i);
		}

		handled = 1;
	}

	return handled;
}

static irqreturn_t ahci_single_edge_irq_intr(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct ahci_host_priv *hpriv;
	unsigned int rc = 0;
	void __iomem *mmio;
	u32 irq_stat, irq_masked;

	VPRINTK("ENTER\n");

	hpriv = host->private_data;
	mmio = hpriv->mmio;

	/* sigh.  0xffffffff is a valid return from h/w */
	irq_stat = readl(mmio + HOST_IRQ_STAT);
	if (!irq_stat)
		return IRQ_NONE;

	irq_masked = irq_stat & hpriv->port_map;

	spin_lock(&host->lock);

	/*
	 * HOST_IRQ_STAT behaves as edge triggered latch meaning that
	 * it should be cleared before all the port events are cleared.
	 */
	writel(irq_stat, mmio + HOST_IRQ_STAT);

	rc = ahci_handle_port_intr(host, irq_masked);

	spin_unlock(&host->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(rc);
}

static irqreturn_t ahci_single_level_irq_intr(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct ahci_host_priv *hpriv;
	unsigned int rc = 0;
	void __iomem *mmio;
	u32 irq_stat, irq_masked;

	VPRINTK("ENTER\n");

	hpriv = host->private_data;
	mmio = hpriv->mmio;

	/* sigh.  0xffffffff is a valid return from h/w */
	irq_stat = readl(mmio + HOST_IRQ_STAT);
	if (!irq_stat)
		return IRQ_NONE;

	irq_masked = irq_stat & hpriv->port_map;

	spin_lock(&host->lock);

	rc = ahci_handle_port_intr(host, irq_masked);

	/* HOST_IRQ_STAT behaves as level triggered latch meaning that
	 * it should be cleared after all the port events are cleared;
	 * otherwise, it will raise a spurious interrupt after each
	 * valid one.  Please read section 10.6.2 of ahci 1.1 for more
	 * information.
	 *
	 * Also, use the unmasked value to clear interrupt as spurious
	 * pending event on a dummy port might cause screaming IRQ.
	 */
	writel(irq_stat, mmio + HOST_IRQ_STAT);

	spin_unlock(&host->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(rc);
}

unsigned int ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;

	/* Keep track of the currently active link.  It will be used
	 * in completion path to determine whether NCQ phase is in
	 * progress.
	 */
	pp->active_link = qc->dev->link;

	if (qc->tf.protocol == ATA_PROT_NCQ)
		writel(1 << qc->tag, port_mmio + PORT_SCR_ACT);

	if (pp->fbs_enabled && pp->fbs_last_dev != qc->dev->link->pmp) {
		u32 fbs = readl(port_mmio + PORT_FBS);
		fbs &= ~(PORT_FBS_DEV_MASK | PORT_FBS_DEC);
		fbs |= qc->dev->link->pmp << PORT_FBS_DEV_OFFSET;
		writel(fbs, port_mmio + PORT_FBS);
		pp->fbs_last_dev = qc->dev->link->pmp;
	}

	writel(1 << qc->tag, port_mmio + PORT_CMD_ISSUE);

	ahci_sw_activity(qc->dev->link);

	return 0;
}
EXPORT_SYMBOL_GPL(ahci_qc_issue);

static bool ahci_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct ahci_port_priv *pp = qc->ap->private_data;
	u8 *rx_fis = pp->rx_fis;

	if (pp->fbs_enabled)
		rx_fis += qc->dev->link->pmp * AHCI_RX_FIS_SZ;

	/*
	 * After a successful execution of an ATA PIO data-in command,
	 * the device doesn't send D2H Reg FIS to update the TF and
	 * the host should take TF and E_Status from the preceding PIO
	 * Setup FIS.
	 */
	if (qc->tf.protocol == ATA_PROT_PIO && qc->dma_dir == DMA_FROM_DEVICE &&
	    !(qc->flags & ATA_QCFLAG_FAILED)) {
		ata_tf_from_fis(rx_fis + RX_FIS_PIO_SETUP, &qc->result_tf);
		qc->result_tf.command = (rx_fis + RX_FIS_PIO_SETUP)[15];
	} else
		ata_tf_from_fis(rx_fis + RX_FIS_D2H_REG, &qc->result_tf);

	return true;
}

static void ahci_freeze(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);

	/* turn IRQ off */
	writel(0, port_mmio + PORT_IRQ_MASK);
}

static void ahci_thaw(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *mmio = hpriv->mmio;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	struct ahci_port_priv *pp = ap->private_data;

	/* clear IRQ */
	tmp = readl(port_mmio + PORT_IRQ_STAT);
	writel(tmp, port_mmio + PORT_IRQ_STAT);
	writel(1 << ap->port_no, mmio + HOST_IRQ_STAT);

	/* turn IRQ back on */
	writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

void ahci_error_handler(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;

#ifdef MY_ABC_HERE
	struct ata_eh_context *host_ehc = &ap->link.eh_context;
	// the work around need to thaw the ata port for reading ncq log
	if (!(ap->pflags & ATA_PFLAG_FROZEN) || host_ehc->i.uiJM585DubiosIFSProtoFlag) {
#else /* MY_ABC_HERE */
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
#endif /* MY_ABC_HERE */
		/* restart engine */
		ahci_stop_engine(ap);
		hpriv->start_engine(ap);
	}

	sata_pmp_error_handler(ap);

	if (!ata_dev_enabled(ap->link.device))
		ahci_stop_engine(ap);
}
EXPORT_SYMBOL_GPL(ahci_error_handler);

static void ahci_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* make DMA engine forget about the failed command */
	if (qc->flags & ATA_QCFLAG_FAILED)
		ahci_kick_engine(ap);
}

static void ahci_set_aggressive_devslp(struct ata_port *ap, bool sleep)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_device *dev = ap->link.device;
	u32 devslp, dm, dito, mdat, deto;
	int rc;
	unsigned int err_mask;

	devslp = readl(port_mmio + PORT_DEVSLP);
	if (!(devslp & PORT_DEVSLP_DSP)) {
		dev_info(ap->host->dev, "port does not support device sleep\n");
		return;
	}

	/* disable device sleep */
	if (!sleep) {
		if (devslp & PORT_DEVSLP_ADSE) {
			writel(devslp & ~PORT_DEVSLP_ADSE,
			       port_mmio + PORT_DEVSLP);
			err_mask = ata_dev_set_feature(dev,
						       SETFEATURES_SATA_DISABLE,
						       SATA_DEVSLP);
			if (err_mask && err_mask != AC_ERR_DEV)
				ata_dev_warn(dev, "failed to disable DEVSLP\n");
		}
		return;
	}

	/* device sleep was already enabled */
	if (devslp & PORT_DEVSLP_ADSE)
		return;

	/* set DITO, MDAT, DETO and enable DevSlp, need to stop engine first */
	rc = ahci_stop_engine(ap);
	if (rc)
		return;

	dm = (devslp & PORT_DEVSLP_DM_MASK) >> PORT_DEVSLP_DM_OFFSET;
	dito = devslp_idle_timeout / (dm + 1);
	if (dito > 0x3ff)
		dito = 0x3ff;

	/* Use the nominal value 10 ms if the read MDAT is zero,
	 * the nominal value of DETO is 20 ms.
	 */
	if (dev->devslp_timing[ATA_LOG_DEVSLP_VALID] &
	    ATA_LOG_DEVSLP_VALID_MASK) {
		mdat = dev->devslp_timing[ATA_LOG_DEVSLP_MDAT] &
		       ATA_LOG_DEVSLP_MDAT_MASK;
		if (!mdat)
			mdat = 10;
		deto = dev->devslp_timing[ATA_LOG_DEVSLP_DETO];
		if (!deto)
			deto = 20;
	} else {
		mdat = 10;
		deto = 20;
	}

	/* Make dito, mdat, deto bits to 0s */
	devslp &= ~GENMASK_ULL(24, 2);
	devslp |= ((dito << PORT_DEVSLP_DITO_OFFSET) |
		   (mdat << PORT_DEVSLP_MDAT_OFFSET) |
		   (deto << PORT_DEVSLP_DETO_OFFSET) |
		   PORT_DEVSLP_ADSE);
	writel(devslp, port_mmio + PORT_DEVSLP);

	hpriv->start_engine(ap);

	/* enable device sleep feature for the drive */
	err_mask = ata_dev_set_feature(dev,
				       SETFEATURES_SATA_ENABLE,
				       SATA_DEVSLP);
	if (err_mask && err_mask != AC_ERR_DEV)
		ata_dev_warn(dev, "failed to enable DEVSLP\n");
}

static void ahci_enable_fbs(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 fbs;
	int rc;

	if (!pp->fbs_supported)
		return;

	fbs = readl(port_mmio + PORT_FBS);
	if (fbs & PORT_FBS_EN) {
		pp->fbs_enabled = true;
		pp->fbs_last_dev = -1; /* initialization */
		return;
	}

	rc = ahci_stop_engine(ap);
	if (rc)
		return;

	writel(fbs | PORT_FBS_EN, port_mmio + PORT_FBS);
	fbs = readl(port_mmio + PORT_FBS);
	if (fbs & PORT_FBS_EN) {
		dev_info(ap->host->dev, "FBS is enabled\n");
		pp->fbs_enabled = true;
		pp->fbs_last_dev = -1; /* initialization */
	} else
		dev_err(ap->host->dev, "Failed to enable FBS\n");

	hpriv->start_engine(ap);
}

static void ahci_disable_fbs(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 fbs;
	int rc;

	if (!pp->fbs_supported)
		return;

	fbs = readl(port_mmio + PORT_FBS);
	if ((fbs & PORT_FBS_EN) == 0) {
		pp->fbs_enabled = false;
		return;
	}

	rc = ahci_stop_engine(ap);
	if (rc)
		return;

	writel(fbs & ~PORT_FBS_EN, port_mmio + PORT_FBS);
	fbs = readl(port_mmio + PORT_FBS);
	if (fbs & PORT_FBS_EN)
		dev_err(ap->host->dev, "Failed to disable FBS\n");
	else {
		dev_info(ap->host->dev, "FBS is disabled\n");
		pp->fbs_enabled = false;
	}

	hpriv->start_engine(ap);
}

static void ahci_pmp_attach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;

	cmd = readl(port_mmio + PORT_CMD);
	cmd |= PORT_CMD_PMP;
	writel(cmd, port_mmio + PORT_CMD);

	ahci_enable_fbs(ap);

	pp->intr_mask |= PORT_IRQ_BAD_PMP;

	/*
	 * We must not change the port interrupt mask register if the
	 * port is marked frozen, the value in pp->intr_mask will be
	 * restored later when the port is thawed.
	 *
	 * Note that during initialization, the port is marked as
	 * frozen since the irq handler is not yet registered.
	 */
	if (!(ap->pflags & ATA_PFLAG_FROZEN))
		writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

static void ahci_pmp_detach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;

	ahci_disable_fbs(ap);

	cmd = readl(port_mmio + PORT_CMD);
	cmd &= ~PORT_CMD_PMP;
	writel(cmd, port_mmio + PORT_CMD);

	pp->intr_mask &= ~PORT_IRQ_BAD_PMP;

	/* see comment above in ahci_pmp_attach() */
	if (!(ap->pflags & ATA_PFLAG_FROZEN))
		writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK);
}

int ahci_port_resume(struct ata_port *ap)
{
	ahci_power_up(ap);
	ahci_start_port(ap);

	if (sata_pmp_attached(ap))
		ahci_pmp_attach(ap);
	else
		ahci_pmp_detach(ap);

	return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_resume);

#ifdef CONFIG_PM
static int ahci_port_suspend(struct ata_port *ap, pm_message_t mesg)
{
	const char *emsg = NULL;
	int rc;

#if defined(MY_DEF_HERE) && defined(MY_ABC_HERE)
	extern int gSynoSystemShutdown;
	struct Scsi_Host *shost;

	if (1 == gSynoSystemShutdown) {
		shost = scsi_host_get(ap->scsi_host);
		if (!shost) {
			printk(KERN_ERR "%s: ata%u can't get scsi host.\n", __func__, ap->print_id);
			goto AP_SUSPEND;
		}
		if (shost->hostt->syno_host_poweroff_task) {
			shost->hostt->syno_host_poweroff_task(shost);
		}
		scsi_host_put(shost);
	}

AP_SUSPEND:
#endif /* MY_DEF_HERE && MY_ABC_HERE */
	rc = ahci_deinit_port(ap, &emsg);
	if (rc == 0)
		ahci_power_down(ap);
	else {
		ata_port_err(ap, "%s (%d)\n", emsg, rc);
		ata_port_freeze(ap);
	}

	return rc;
}
#endif

static int ahci_port_start(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct device *dev = ap->host->dev;
	struct ahci_port_priv *pp;
	void *mem;
	dma_addr_t mem_dma;
	size_t dma_sz, rx_fis_sz;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	if (ap->host->n_ports > 1) {
		pp->irq_desc = devm_kzalloc(dev, 8, GFP_KERNEL);
		if (!pp->irq_desc) {
			devm_kfree(dev, pp);
			return -ENOMEM;
		}
		snprintf(pp->irq_desc, 8,
			 "%s%d", dev_driver_string(dev), ap->port_no);
	}

	/* check FBS capability */
	if ((hpriv->cap & HOST_CAP_FBS) && sata_pmp_supported(ap)) {
		void __iomem *port_mmio = ahci_port_base(ap);
		u32 cmd = readl(port_mmio + PORT_CMD);
		if (cmd & PORT_CMD_FBSCP)
			pp->fbs_supported = true;
		else if (hpriv->flags & AHCI_HFLAG_YES_FBS) {
			dev_info(dev, "port %d can do FBS, forcing FBSCP\n",
				 ap->port_no);
			pp->fbs_supported = true;
		} else
			dev_warn(dev, "port %d is not capable of FBS\n",
				 ap->port_no);
	}

	if (pp->fbs_supported) {
		dma_sz = AHCI_PORT_PRIV_FBS_DMA_SZ;
		rx_fis_sz = AHCI_RX_FIS_SZ * 16;
	} else {
		dma_sz = AHCI_PORT_PRIV_DMA_SZ;
		rx_fis_sz = AHCI_RX_FIS_SZ;
	}

	mem = dmam_alloc_coherent(dev, dma_sz, &mem_dma, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	memset(mem, 0, dma_sz);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot = mem;
	pp->cmd_slot_dma = mem_dma;

	mem += AHCI_CMD_SLOT_SZ;
	mem_dma += AHCI_CMD_SLOT_SZ;

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = mem;
	pp->rx_fis_dma = mem_dma;

	mem += rx_fis_sz;
	mem_dma += rx_fis_sz;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = mem;
	pp->cmd_tbl_dma = mem_dma;

	/*
	 * Save off initial list of interrupts to be enabled.
	 * This could be changed later
	 */
	pp->intr_mask = DEF_PORT_IRQ;

	/*
	 * Switch to per-port locking in case each port has its own MSI vector.
	 */
	if (hpriv->flags & AHCI_HFLAG_MULTI_MSI) {
		spin_lock_init(&pp->lock);
		ap->lock = &pp->lock;
	}

	ap->private_data = pp;

	/* engage engines, captain */
	return ahci_port_resume(ap);
}

static void ahci_port_stop(struct ata_port *ap)
{
	const char *emsg = NULL;
	int rc;

	/* de-initialize port */
	rc = ahci_deinit_port(ap, &emsg);
	if (rc)
		ata_port_warn(ap, "%s (%d)\n", emsg, rc);
}

void ahci_print_info(struct ata_host *host, const char *scc_s)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	u32 vers, cap, cap2, impl, speed;
	const char *speed_s;

	vers = readl(mmio + HOST_VERSION);
	cap = hpriv->cap;
	cap2 = hpriv->cap2;
	impl = hpriv->port_map;

	speed = (cap >> 20) & 0xf;
	if (speed == 1)
		speed_s = "1.5";
	else if (speed == 2)
		speed_s = "3";
	else if (speed == 3)
		speed_s = "6";
	else
		speed_s = "?";

	dev_info(host->dev,
		"AHCI %02x%02x.%02x%02x "
		"%u slots %u ports %s Gbps 0x%x impl %s mode\n"
		,

		(vers >> 24) & 0xff,
		(vers >> 16) & 0xff,
		(vers >> 8) & 0xff,
		vers & 0xff,

		((cap >> 8) & 0x1f) + 1,
		(cap & 0x1f) + 1,
		speed_s,
		impl,
		scc_s);

	dev_info(host->dev,
		"flags: "
		"%s%s%s%s%s%s%s"
		"%s%s%s%s%s%s%s"
		"%s%s%s%s%s%s%s"
		"%s%s\n"
		,

		cap & HOST_CAP_64 ? "64bit " : "",
		cap & HOST_CAP_NCQ ? "ncq " : "",
		cap & HOST_CAP_SNTF ? "sntf " : "",
		cap & HOST_CAP_MPS ? "ilck " : "",
		cap & HOST_CAP_SSS ? "stag " : "",
		cap & HOST_CAP_ALPM ? "pm " : "",
		cap & HOST_CAP_LED ? "led " : "",
		cap & HOST_CAP_CLO ? "clo " : "",
		cap & HOST_CAP_ONLY ? "only " : "",
		cap & HOST_CAP_PMP ? "pmp " : "",
		cap & HOST_CAP_FBS ? "fbs " : "",
		cap & HOST_CAP_PIO_MULTI ? "pio " : "",
		cap & HOST_CAP_SSC ? "slum " : "",
		cap & HOST_CAP_PART ? "part " : "",
		cap & HOST_CAP_CCC ? "ccc " : "",
		cap & HOST_CAP_EMS ? "ems " : "",
		cap & HOST_CAP_SXS ? "sxs " : "",
		cap2 & HOST_CAP2_DESO ? "deso " : "",
		cap2 & HOST_CAP2_SADM ? "sadm " : "",
		cap2 & HOST_CAP2_SDS ? "sds " : "",
		cap2 & HOST_CAP2_APST ? "apst " : "",
		cap2 & HOST_CAP2_NVMHCI ? "nvmp " : "",
		cap2 & HOST_CAP2_BOH ? "boh " : ""
		);
}
EXPORT_SYMBOL_GPL(ahci_print_info);

void ahci_set_em_messages(struct ahci_host_priv *hpriv,
			  struct ata_port_info *pi)
{
	u8 messages;
	void __iomem *mmio = hpriv->mmio;
	u32 em_loc = readl(mmio + HOST_EM_LOC);
	u32 em_ctl = readl(mmio + HOST_EM_CTL);

	if (!ahci_em_messages || !(hpriv->cap & HOST_CAP_EMS))
		return;

	messages = (em_ctl & EM_CTRL_MSG_TYPE) >> 16;

	if (messages) {
		/* store em_loc */
		hpriv->em_loc = ((em_loc >> 16) * 4);
		hpriv->em_buf_sz = ((em_loc & 0xff) * 4);
		hpriv->em_msg_type = messages;
		pi->flags |= ATA_FLAG_EM;
		if (!(em_ctl & EM_CTL_ALHD))
			pi->flags |= ATA_FLAG_SW_ACTIVITY;
#ifdef MY_DEF_HERE
		if (em_ctl & EM_CTL_LED) {
			pi->flags |= ATA_FLAG_SW_LOCATE;
			pi->flags |= ATA_FLAG_SW_FAULT;
		}
#endif /* MY_DEF_HERE */
	}
}
EXPORT_SYMBOL_GPL(ahci_set_em_messages);

static int ahci_host_activate_multi_irqs(struct ata_host *host, int irq,
					 struct scsi_host_template *sht)
{
	int i, rc;

	rc = ata_host_start(host);
	if (rc)
		return rc;
	/*
	 * Requests IRQs according to AHCI-1.1 when multiple MSIs were
	 * allocated. That is one MSI per port, starting from @irq.
	 */
	for (i = 0; i < host->n_ports; i++) {
		struct ahci_port_priv *pp = host->ports[i]->private_data;

		/* Do not receive interrupts sent by dummy ports */
		if (!pp) {
			disable_irq(irq + i);
			continue;
		}

		rc = devm_request_threaded_irq(host->dev, irq + i,
#ifdef MY_ABC_HERE
					       syno_ahci_multi_irqs_intr,
#else /* MY_ABC_HERE */
					       ahci_multi_irqs_intr,
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
					       syno_ahci_port_thread_fn, 0,
#else /* MY_DEF_HERE */
					       ahci_port_thread_fn, 0,
#endif /* MY_DEF_HERE */
					       pp->irq_desc, host->ports[i]);
		if (rc)
			return rc;
		ata_port_desc(host->ports[i], "irq %d", irq + i);
	}
	return ata_host_register(host, sht);
}

#ifdef MY_DEF_HERE
#ifdef MY_ABC_HERE
static bool syno_internal_slot_check(struct ata_port* ap)
{
	struct device_node *pSlotNode = NULL;
	struct device_node *pAhciNode = NULL;
	bool blRet = false;
	int index = 0;

	if (!ap) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (ap->ops->syno_compare_node_info(ap, pSlotNode)) {
			break;
		}
	}

	if (!pSlotNode) {
		goto END;
	}

	if (pSlotNode->full_name && 1 == sscanf(pSlotNode->full_name, "/"DT_INTERNAL_SLOT"@%d", &index)) {
		/*
		 * At first, we only apply internal slot mode on those models which have "internal_mode" attr in dts
		 * If we want to apply to all ahci models one day, just remove the comparison as below
		 */
		if (NULL == (pAhciNode = of_get_child_by_name(pSlotNode, DT_AHCI))) {
			goto END;
		}
		if (of_property_read_bool(pAhciNode, DT_AHCI_INTERNAL_MODE)) {
			blRet = true;
			goto END;
		}
	}

END:
	if (pAhciNode) {
		of_node_put(pAhciNode);
	}
	if (pSlotNode) {
		of_node_put(pSlotNode);
	}
	return blRet;
}
#else /* MY_ABC_HERE */
extern bool gSynoAtaInternal[MAX_INTERNAL_ATA_PORT];
static bool syno_internal_slot_check(struct ata_port* ap)
{
	bool blRet = false;

	if (NULL == ap || 0 >= ap->print_id || MAX_INTERNAL_ATA_PORT < ap->print_id) {
		goto END;
	}
	blRet = gSynoAtaInternal[ap->print_id - 1];

END:
	return blRet;
}
#endif /* MY_ABC_HERE */
#endif /* MY_DEF_HERE */

/**
 *	ahci_host_activate - start AHCI host, request IRQs and register it
 *	@host: target ATA host
 *	@sht: scsi_host_template to use when registering the host
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ahci_host_activate(struct ata_host *host, struct scsi_host_template *sht)
{
	struct ahci_host_priv *hpriv = host->private_data;
	int irq = hpriv->irq;
	int rc;
#ifdef MY_DEF_HERE
	int i = 0;
	struct ata_port *ap = NULL;
#endif /* MY_DEF_HERE */

	if (hpriv->flags & AHCI_HFLAG_MULTI_MSI)
		rc = ahci_host_activate_multi_irqs(host, irq, sht);
	else if (hpriv->flags & AHCI_HFLAG_EDGE_IRQ)
		rc = ata_host_activate(host, irq, ahci_single_edge_irq_intr,
				       IRQF_SHARED, sht);
	else
		rc = ata_host_activate(host, irq, ahci_single_level_irq_intr,
				       IRQF_SHARED, sht);
#ifdef MY_DEF_HERE
	for (i = 0; i < host->n_ports; i++) {
		ap = host->ports[i];
		if (syno_internal_slot_check(ap)) {
			ap->syno_ahci_handle_port_interrupt = &syno_internal_ahci_handle_port_interrupt;
		} else {
			ap->syno_ahci_handle_port_interrupt = &ahci_handle_port_interrupt;
		}
	}
#endif /* MY_DEF_HERE */
	return rc;
}
EXPORT_SYMBOL_GPL(ahci_host_activate);

#if defined(MY_ABC_HERE)
#if defined(MY_DEF_HERE)
/**
 *	syno_set_blink - Set led-blinking state of given ata port
 *	@ap: target ata port
 *	@state: 0 for SYNO_LED_BLINK_OFF; 1 for SYNO_LED_BLINK_ON
 *
 *  The implementation reads information of pin from device tree, and
 *	set it by input state.
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int syno_set_blink(struct ata_port* ap, u32 state)
{
	int ret = -EINVAL;
	struct device_node *pSlotNode = NULL;
	struct device_node *pLedNode = NULL;
	u32 pinInfo[SYNO_GPIO_INDEX_MAX] = {0};

	if (!ap) {
		goto Err;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (ap->ops->syno_compare_node_info(ap, pSlotNode)) {
			break;
		}
	}
	if (!pSlotNode) {
		printk(KERN_ERR "Cannot find slot node of this ata_port.\n");
		goto Err;
	}

	pLedNode = of_get_child_by_name(pSlotNode, DT_HDD_ACT_LED);
	of_node_put(pSlotNode);
	if (!pLedNode) {
		printk(KERN_WARNING "No ACT LED pin.\n");
		goto Err;
	}

	if (of_property_read_u32_array(pLedNode, DT_SYNO_GPIO, pinInfo, SYNO_GPIO_INDEX_MAX)) {
		printk(KERN_ERR "Cannot find pin information.\n");
		goto Err;
	}
	of_node_put(pLedNode);

	if (pinInfo[SYNO_POLARITY_PIN] == state) {
		SYNO_GPIO_WRITE(pinInfo[SYNO_GPIO_PIN], 1);
	} else {
		SYNO_GPIO_WRITE(pinInfo[SYNO_GPIO_PIN], 0);
	}
	ret = 0;

Err:
	return ret;
}
#endif /* MY_DEF_HERE */

#if defined(MY_ABC_HERE) || defined(MY_DEF_HERE)
/**
 *	syno_get_prop_sw_activity - Get Extended AHCI attributes 
	named sw_activity
 *	@ap: target ata port
 *
 *  The implementation reads extended AHCI attributes
 *  This function aims to replace hard-coded model check of
 *  syno_need_ahci_software_activity but not limited to it.
 *
 *	RETURNS:
 *	0 if not found or failed, else returns sw_activity value.
 */
static u32 syno_get_prop_sw_activity(struct ata_port* ap)
{
	struct device_node *pSlotNode = NULL;
	struct device_node *pAhciNode = NULL;
	u32 sw_activity = 0; /* Do NOT set software activity by default */

	if (!ap) {
		goto Err;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (ap->ops->syno_compare_node_info(ap, pSlotNode)) {
			break;
		}
	}

	if (!pSlotNode) {
		printk(KERN_ERR "Cannot find slot node of this ata_port.\n");
		goto Err;
	}

	pAhciNode = of_get_child_by_name(pSlotNode, DT_AHCI);
	of_node_put(pSlotNode);
	if (!pAhciNode) {
		printk(KERN_WARNING "No AHCI child node.\n");
		goto Err;
	}

	if (of_property_read_u32(pAhciNode, DT_PROPERTY_SW_ACTIVITY, &sw_activity)) {
		sw_activity = 0; /* Do NOT set software activity if NOT found */
	}
	of_node_put(pAhciNode);

Err:
	return sw_activity;
}
#endif /* defined(MY_ABC_HERE) || defined(MY_DEF_HERE) */
#endif /* MY_ABC_HERE */

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Common AHCI SATA low-level routines");
MODULE_LICENSE("GPL");
