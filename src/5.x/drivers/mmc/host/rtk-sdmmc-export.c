// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek SD/MMC/mini SD card driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

static int initial_flag2;

int get_RTK_initial_flag(void) {
	return initial_flag2;
}
EXPORT_SYMBOL(get_RTK_initial_flag);

void set_RTK_initial_flag(int flag) {
	initial_flag2 = flag;
}
EXPORT_SYMBOL(set_RTK_initial_flag);
