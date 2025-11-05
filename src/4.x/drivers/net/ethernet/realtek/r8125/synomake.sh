#!/bin/bash


cd /opt/dsm-kernel/$TARGET_PLATFORM
head -21 make_modules.sh > /tmp/dsmmake
. /tmp/dsmmake
#cd /opt/dsm-kernel/synology-igc
#cd /$KERNEL_SOURCE/drivers/net/ethernet/qlogic/netxen
cd /opt/dsm-kernel/r8125-9.010.01/src
#dsmmake
#dsmmake CONFIG_NETXEN_NIC=m
dsmmake 
strip --strip-debug *.ko 
cp *.ko /opt/dsm-kernel/modules_make/$TARGET_PLATFORM/
ls -ltr /opt/dsm-kernel/modules_make/$TARGET_PLATFORM/r8125.ko
modinfo /opt/dsm-kernel/modules_make/$TARGET_PLATFORM/r8125.ko
rm -rf *.ko *.o


