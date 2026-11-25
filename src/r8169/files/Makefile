# SPDX-License-Identifier: GPL-2.0-only
################################################################################
#
# r8169 is the Linux device driver released for RealTek RTL8169S/8110S,
# RTL8169SB/8110SB, and RTL8110SC Gigabit Ethernet controllers with
# PCI interface.
#
# Copyright(c) 2024 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################

################################################################################
# This product is covered by one or more of the following patents:
# US5,307,459, US5,434,872, US5,732,094, US6,570,884, US6,115,776, and US6,327,625.
################################################################################

ENABLE_S5_KEEP_CURR_MAC = n

ifneq ($(KERNELRELEASE),)
	obj-m := r8169.o
	r8169-objs := r8169_n.o
	EXTRA_CFLAGS += -DCONFIG_R8169_NAPI
	EXTRA_CFLAGS += -DCONFIG_R8169_VLAN
	ifeq ($(ENABLE_S5_KEEP_CURR_MAC), y)
		EXTRA_CFLAGS += -DENABLE_S5_KEEP_CURR_MAC
	endif
else
	BASEDIR := /lib/modules/$(shell uname -r)
	KERNELDIR ?= $(BASEDIR)/build
	PWD :=$(shell pwd)
	DRIVERDIR := $(shell find $(BASEDIR)/kernel/drivers/net/ethernet -name realtek -type d)
	ifeq ($(DRIVERDIR),)
		DRIVERDIR := $(shell find $(BASEDIR)/kernel/drivers/net -name realtek -type d)
	endif
	ifeq ($(DRIVERDIR),)
		DRIVERDIR := $(BASEDIR)/kernel/drivers/net
	endif
	RTKDIR := $(subst $(BASEDIR)/,,$(DRIVERDIR))

	KERNEL_GCC_VERSION := $(shell cat /proc/version | sed -n 's/.*gcc version \([[:digit:]]\.[[:digit:]]\.[[:digit:]]\).*/\1/p')
	CCVERSION = $(shell $(CC) -dumpversion)

	KVER = $(shell uname -r)
	KMAJ = $(shell echo $(KVER) | \
	sed -e 's/^\([0-9][0-9]*\)\.[0-9][0-9]*\.[0-9][0-9]*.*/\1/')
	KMIN = $(shell echo $(KVER) | \
	sed -e 's/^[0-9][0-9]*\.\([0-9][0-9]*\)\.[0-9][0-9]*.*/\1/')
	KREV = $(shell echo $(KVER) | \
	sed -e 's/^[0-9][0-9]*\.[0-9][0-9]*\.\([0-9][0-9]*\).*/\1/')

	kver_ge = $(shell \
	echo test | awk '{if($(KMAJ) < $(1)) {print 0} else { \
	if($(KMAJ) > $(1)) {print 1} else { \
	if($(KMIN) < $(2)) {print 0} else { \
	if($(KMIN) > $(2)) {print 1} else { \
	if($(KREV) < $(3)) {print 0} else { print 1 } \
	}}}}}' \
	)

.PHONY: all
all: print_vars clean modules install

print_vars:
	@echo
	@echo "CC: " $(CC)
	@echo "CCVERSION: " $(CCVERSION)
	@echo "KERNEL_GCC_VERSION: " $(KERNEL_GCC_VERSION)
	@echo "KVER: " $(KVER)
	@echo "KMAJ: " $(KMAJ)
	@echo "KMIN: " $(KMIN)
	@echo "KREV: " $(KREV)
	@echo "BASEDIR: " $(BASEDIR)
	@echo "DRIVERDIR: " $(DRIVERDIR)
	@echo "PWD: " $(PWD)
	@echo "RTKDIR: " $(RTKDIR)
	@echo

.PHONY:modules
modules:
#ifeq ($(call kver_ge,5,0,0),1)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
#else
#	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules
#endif

.PHONY:clean
clean:
#ifeq ($(call kver_ge,5,0,0),1)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
#else
#	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) clean
#endif

.PHONY:install
install:
#ifeq ($(call kver_ge,5,0,0),1)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) INSTALL_MOD_DIR=$(RTKDIR) modules_install
#else
#	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) INSTALL_MOD_DIR=$(RTKDIR) modules_install
#endif

endif
