#
#
#

CONFIG_CTAP_SHORT = ON

obj-$(CONFIG_R8152) := r8152.o
#ccflags-y += -DRTL8152_S5_WOL
#ccflags-y += -DRTL8152_DEBUG

ifneq (,$(filter OFF off, $(CONFIG_CTAP_SHORT)))
	ccflags-y += -DCONFIG_CTAP_SHORT_OFF
endif

ifeq (TRUE, $(shell test $(VERSION) -lt 5 && echo "TRUE" || \
	test $(VERSION) -eq 5 && test $(PATCHLEVEL) -lt 12 && echo "TRUE"))
	ccflags-y += -DLINUX_VERSION_MAJOR=$(VERSION)
	ccflags-y += -DLINUX_VERSION_PATCHLEVEL=$(PATCHLEVEL)
	ccflags-y += -DLINUX_VERSION_SUBLEVEL=$(SUBLEVEL)
endif
