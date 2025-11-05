# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Realtek network device drivers.
#

obj-$(CONFIG_8139CP) += 8139cp.o
#obj-$(CONFIG_8139TOO) += 8139too.o
#obj-$(CONFIG_ATP) += atp.o
obj-$(CONFIG_R8168) += r8168/
obj-$(CONFIG_R8125) += r8125/
obj-$(CONFIG_R8126) += r8126/
obj-$(CONFIG_R8126) += r8127/

ifeq ($(CONFIG_R8168), m)
	EXTRA_CFLAGS += -DCONFIG_R8168
endif

ifeq ($(CONFIG_R8125), m)
	EXTRA_CFLAGS += -DCONFIG_R8125
endif

r8169-objs += r8169_main.o r8169_firmware.o r8169_phy_config.o
obj-$(CONFIG_R8169) += r8169.o

