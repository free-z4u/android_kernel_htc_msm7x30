####v3.17
git checkout HEAD -- crypto/sha512_generic.c arch/arm/crypto/aes-armv4.S  arch/arm/crypto/aes_glue.c  arch/arm/crypto/sha1-armv4-large.S arch/arm/crypto/sha1_glue.c

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
# unable back FREQ_MSM 77b0999d490765a88edbdb1bba3fc3147bb9e782
# clean up include/linux/usb/phy.h
# use devicetree for qcom,pm8058-pwrkey;qcom,pm8058-rtc
# use official include/linux/pwm.h
# drop usage of MFD_PM8XXX_IRQ - bc866fc7a8c4322de40b694ffcfcdda50ab82f35
# drop alternative version of drivers/i2c/busses/i2c-qup.c - 39de65aa2c3eee901db020a4f1396998e09602a3

#!/bin/sh
LIST=`git diff v3.15 --name-only | grep -v "\.h" | grep -v "Makefile" | grep -v "Kconfig" | sed 's|\.c|\.o|g'`
for i in $LIST; do
    if [ ! -f $i ]; then
	GFILE=`echo $i | sed 's|\.o|\.c|g'`
	echo $GFILE
	# git checkout v3.15 -- $GFILE
    fi 
done
