// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek SDIO host driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#ifndef _DRIVERS_MMC_SDHCI_OF_RTK_H
#define _DRIVERS_MMC_SDHCI_OF_RTK_H

#include "sdhci.h"

void disable_sdio_irq(struct sdhci_host *host);
void rtk_sdhci_close_clk(void);
void rtk_register_set(void);

#endif /* _DRIVERS_MMC_SDHCI_OF_RTK_H */
