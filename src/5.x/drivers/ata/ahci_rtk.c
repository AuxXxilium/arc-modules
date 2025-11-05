// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek ahci driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/reset.h>

#include <linux/ahci_platform.h>
#include "ahci.h"

#define DRV_NAME		"rtk_ahci"
#define RTK_SATA_MAX_PORT	2

#define REG_CLKEN		0x18
#define MAC_CLKEN		BIT(6)
#define MAC_PORT0_EN		BIT(0) | BIT(1) | BIT(2)
#define MAC_PORT1_EN		BIT(3) | BIT(4) | BIT(5)

#define REG_SRAM_CTL3		0xf0
#define SRAM_SHARE		BIT(0)
#define CLEAR_ERR		BIT(2)

enum host_state {
	INITIAL = 0,
	SUSPEND,
	RESUME,
	RUNNING
};

struct rtk_ahci_port {
	struct reset_control *rst;
};

struct rtk_ahci_priv {
	struct device *dev;
	struct ahci_host_priv *hpriv;
	struct regmap *wrapper;
	struct rtk_ahci_port **ports;
	struct delayed_work work;

	enum host_state state;
	unsigned int hostinit;
};

static const struct ata_port_info ahci_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_platform_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static void rtk_sata_init(struct ahci_host_priv *hpriv, unsigned int port)
{
	struct rtk_ahci_priv *priv = hpriv->plat_data;

	regmap_update_bits(priv->wrapper, REG_CLKEN, MAC_CLKEN, MAC_CLKEN);

	regmap_update_bits(priv->wrapper, REG_SRAM_CTL3,
			   SRAM_SHARE | CLEAR_ERR, SRAM_SHARE | CLEAR_ERR);

	if (port == 0)
		regmap_update_bits(priv->wrapper, REG_CLKEN,
				   MAC_PORT0_EN , MAC_PORT0_EN);
	else
		regmap_update_bits(priv->wrapper, REG_CLKEN,
				   MAC_PORT1_EN , MAC_PORT1_EN);
}

static int rtk_sata_host_resume(struct rtk_ahci_priv *priv)
{
	struct ata_host *host = dev_get_drvdata(priv->dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct ata_port *ap;
	int cnt = 0, i;

	for (i = 0; i < hpriv->nports; i++) {
		ap = host->ports[i];
		if (ap->link.sata_spd && ap->scsi_host->shost_state == SHOST_RUNNING)
			cnt++;
		else if (!ap->link.sata_spd)
			cnt++;
	}
	if (cnt < hpriv->nports)
		return -1;

	return 0;
}

static void rtk_sata_host_ctrl(struct work_struct *work)
{
	struct rtk_ahci_priv *priv = container_of(work, struct rtk_ahci_priv, work.work);
	struct device *dev = priv->dev;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	switch (priv->state) {
	case INITIAL:
		ahci_platform_init_host(pdev, priv->hpriv, &ahci_port_info,
					&ahci_platform_sht);
		priv->state = RUNNING;
		break;
	case RUNNING:
		pr_err("host is running\n");
		break;
	case RESUME:
		if (rtk_sata_host_resume(priv))
			priv->state = RUNNING;
		else
			schedule_delayed_work(&priv->work, 2*HZ);
		break;
	case SUSPEND:
	default:
		dev_err(priv->dev, "state error, do nothing\n");
		break;
	}
}

static int rtk_ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child;
	struct ahci_host_priv *hpriv;
	struct rtk_ahci_priv *priv;
	struct rtk_ahci_port *port;
	unsigned int index = 0;
	unsigned int portid;
	struct gpio_desc *powerio;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	priv->wrapper = syscon_regmap_lookup_by_phandle(node, "realtek,satawrap");
	if (IS_ERR(priv->wrapper)) {
		dev_err(dev, "failed to remap sata wrapper reg\n");
		return PTR_ERR(priv->wrapper);
	}

	hpriv = ahci_platform_get_resources(pdev, AHCI_PLATFORM_GET_RESETS);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	if (hpriv->nports > RTK_SATA_MAX_PORT || hpriv->nports == 0) {
		dev_err(dev, "port number can't not support\n");
		return -ENODEV;
	}
	hpriv->plat_data = priv;
	priv->hpriv = hpriv;

	priv->ports = devm_kcalloc(dev, hpriv->nports, sizeof(*priv->ports), GFP_KERNEL);
	if (!priv->ports)
		return -ENOMEM;

	for_each_available_child_of_node(node, child) {
		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		if (of_property_read_u32(child, "reg", &portid))
			if (portid >= RTK_SATA_MAX_PORT)
				continue;

		powerio = gpiod_get_from_of_node(child, "sata-gpios", 0, GPIOD_OUT_HIGH, child->name);
		if (!IS_ERR(powerio)) {
			//gpiod_put(powerio);
		}
		port->rst = of_reset_control_get_by_index(child, 0);
		if (IS_ERR(port->rst))
			return PTR_ERR(port->rst);
		reset_control_deassert(port->rst);

		priv->ports[index++] = port;

		rtk_sata_init(hpriv, portid);
	}

	ahci_platform_enable_resources(hpriv);

	hpriv->flags |= AHCI_HFLAG_YES_FBS;
	writel((readl(hpriv->mmio + HOST_PORTS_IMPL) | 3),
		hpriv->mmio + HOST_PORTS_IMPL);

	INIT_DELAYED_WORK(&priv->work, rtk_sata_host_ctrl);

	priv->state = INITIAL;
	of_property_read_u32(dev->of_node, "hostinit-mode", &priv->hostinit);
	if (priv->hostinit) {
		ahci_platform_init_host(pdev, hpriv, &ahci_port_info,
					&ahci_platform_sht);
		priv->state = RUNNING;
	}

	schedule_delayed_work(&priv->work, 1*HZ);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rtk_ahci_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct rtk_ahci_priv *priv = hpriv->plat_data;
	struct device_node *child;
	int rc;
	struct gpio_desc *powerio;

	cancel_delayed_work(&priv->work);
	priv->state = SUSPEND;

	rc = ahci_platform_suspend(dev);
	if (rc)
		return rc;

	for_each_available_child_of_node(dev->of_node, child) {
		powerio = gpiod_get_from_of_node(child, "sata-gpios", 0, GPIOD_OUT_LOW, child->name);
		if (!IS_ERR(powerio)) {
			gpiod_put(powerio);
		}
	}

	return 0;
}

static int rtk_ahci_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct rtk_ahci_priv *priv = hpriv->plat_data;
	struct device_node *child;
	int rc, i;
	struct gpio_desc *powerio;

	for (i=0; i<hpriv->nports; i++) {
		if (priv->ports[i]->rst != NULL) {
			reset_control_deassert(priv->ports[i]->rst);
		}
		rtk_sata_init(hpriv, i);
	}

	ahci_platform_enable_resources(hpriv);

	writel((readl(hpriv->mmio + HOST_PORTS_IMPL) | 3),
		hpriv->mmio + HOST_PORTS_IMPL);

	rc = ahci_platform_resume_host(dev);
	if (rc)
		return rc;

	/* We resumed so update PM runtime state */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	for_each_available_child_of_node(dev->of_node, child) {
		powerio = gpiod_get_from_of_node(child, "sata-gpios", 0, GPIOD_OUT_HIGH, child->name);
		if (!IS_ERR(powerio)) {
			gpiod_put(powerio);
		}
	}

	priv->state = RESUME;
	schedule_delayed_work(&priv->work, 3*HZ);

	return 0;
}

static const struct dev_pm_ops rtk_ahci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rtk_ahci_suspend, rtk_ahci_resume)
};

#define DEV_PM_OPS	(&rtk_ahci_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static const struct of_device_id rtk_ahci_of_match[] = {
	{ .compatible = "realtek,ahci-sata", },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_ahci_of_match);

static struct platform_driver rtk_ahci_driver = {
	.probe = rtk_ahci_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = rtk_ahci_of_match,
		.pm = DEV_PM_OPS,
	},
};
module_platform_driver(rtk_ahci_driver);

MODULE_DESCRIPTION("RTK AHCI SATA platform driver");
MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahci");
