####v3.17
git checkout HEAD -- arch/arm/crypto/aes-armv4.S

####v3.18
git checkout HEAD -- lib/lzo/lzo1x_decompress_safe.c

# for recheck: lib/genalloc.c include/linux/genalloc.h
# use code from 78be3176c4335b8ff3d9625ed3fc571e7d2ba8f4 for reboot chip msm7x30_reset
# rewrite arch/arm/mach-msm/clock-7x30.c:msm_clocks_7x30 to official way
# include/linux/pwm.h drivers/misc/pmic8058-pwm.c to official way
# drivers/gpu/ion/ion_carveout_heap.c - ion_*_region -> *_region
# drivers/usb/gadget/udc-core.c - use only new style codes
# drivers/usb/gadget/android.c rewrite all this code to something useful
# net/ipv6/exthdrs_core.c - fix code
# need to rewrite use Device Drivers -> USB Support -> USB Gadget Support -> USB Gadget Driver -> Function Filesystem
# https://groups.google.com/forum/#!topic/android-kernel/EDPxbSze-6Q
# clean up include/linux/usb/phy.h
# use devicetree for qcom,pm8058-pwrkey;qcom,pm8058-rtc
# use official include/linux/pwm.h
# drop usage of MFD_PM8XXX_IRQ - bc866fc7a8c4322de40b694ffcfcdda50ab82f35
# drop alternative version of drivers/i2c/busses/i2c-qup.c - 39de65aa2c3eee901db020a4f1396998e09602a3
# init/main.c - initcall issues?
# arch/arm/include/asm/memblock.h - issue with meminfo
# arch/arm/include/asm/setup.h - issue with meminfo
# arch/arm/kernel/devtree.c - issue with meminfo
# arch/arm/kernel/setup.c - issue with meminfo
# arch/arm/mm/init.c - issue with meminfo
# arch/arm/mm/mmu.c - issue with meminfo
# staging changes:
# drivers/staging/android/Kconfig
# drivers/staging/android/Makefile
# drivers/staging/android/sw_sync.c
# drivers/staging/android/sync.c
# drivers/staging/android/sync_debug.c
# drivers/staging/android/trace/sync.h
# include/linux/sync.h
# drivers/base/Kconfig
# drivers/base/firmware_class.c
# include/linux/firmware.h
# Documentation/ABI/testing/sysfs-bus-platform
# drivers/base/platform.c
# include/linux/platform_device.h


#!/bin/sh
LIST=`git diff v3.15 --name-only | grep -v "\.h" | grep -v "Makefile" | grep -v "Kconfig" | sed 's|\.c|\.o|g'`
for i in $LIST; do
    if [ ! -f $i ]; then
	GFILE=`echo $i | sed 's|\.o|\.c|g'`
	echo $GFILE
	# git checkout v3.15 -- $GFILE
    fi 
done
