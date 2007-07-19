#!/bin/bash
#
# Copyright (C) 2004-2006 Jon Gettler
# http://www.mvpmc.org/
#
# This script will copy the IBM kernel modules from a Hauppauge dongle.bin
# bootfile and build a new bootable mediamvp file from the supplied filesystem.
#

set -e

TMP="tmpdir"
RAMDISK="ramdisk"

export PATH=$PATH:bin

if [ -a $TMP ] ; then
	rm -rf $TMP
fi
if [ -a $RAMDISK ] ; then
	rm -rf $RAMDISK
fi

print_help() {
	echo "$0 [-d dongle.bin] [-k kernel dir] [-o outfile]"
}

error() {
	echo "ERROR: $1"

	exit 1
}

mkdir $TMP || error "failed to create directory $TMP"
touch $RAMDISK || error "failed to create file $RAMDISK"

#
# make_dongle() - this is straight out of arch/ppc in the linux tree
#
make_dongle() {
	KERN=$1
	RD=$2

	if [ "$CROSS" == "" ] ; then
	    CROSS=bin/powerpc-405-linux-gnu-
	fi
	OBJCOPY=${CROSS}objcopy
	LD=${CROSS}ld

$OBJCOPY -O elf32-powerpc \
	--add-section=.ramdisk=${RD} \
	--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
	--add-section=.image=${KERN} \
	--set-section-flags=.image=contents,alloc,load,readonly,data \
	filesystem/kernel_files/dummy.o ${TMP}/image.o
$LD -T filesystem/kernel_files/ld.script -Ttext 0x00400000 -Bstatic -o ${TMP}/zvmlinux.initrd filesystem/kernel_files/head.o filesystem/kernel_files/relocate.o  filesystem/kernel_files/misc-embedded.o filesystem/kernel_files/misc-common.o filesystem/kernel_files/string.o filesystem/kernel_files/util.o filesystem/kernel_files/embed_config.o filesystem/kernel_files/ns16550.o ${TMP}/image.o filesystem/kernel_files/zlib.a ${SERIAL_STUB}
$OBJCOPY -O elf32-powerpc ${TMP}/zvmlinux.initrd ${TMP}/zvmlinux.initrd -R .comment -R .stab -R .stabstr \
	-R .sysmap
../tools/mktree/mktree ${TMP}/zvmlinux.initrd ${OUTFILE} || error "mktree failed"
}

while [ "$1" ] ; do
	case $1 in
		-d)
			DONGLE=$2
			shift 2
			;;
		-o)
			OUTFILE=$2
			shift 2
			;;
		*)
			shift
			;;
	esac
done

. filesystem/kernel_files/version
if [ "$KERNELVER" = "2.4.31" ] ; then
    SERIAL_STUB=filesystem/kernel_files/serial_stub.o
    EXTRAVER=-v1.1-hcwmvp
else
    SERIAL_STUB=
    EXTRAVER=_mvl21-vdongle
fi

#
# Copy the kernel libraries into the fs dir
#
if [ "$KERNELVER" = "2.4.31" ] ; then
    MODDIR=filesystem/install_wrapper/lib/modules
else
    MODDIR=filesystem/install/lib/modules
fi

rm -rf filesystem/install/lib/modules
rm -rf filesystem/install_wrapper/lib/modules

mkdir -p ${MODDIR}/${KERNELVER}${EXTRAVER}/misc
cp filesystem/tree/lib/modules/${KERNELVER}${EXTRAVER}/misc/*.o ${MODDIR}/${KERNELVER}${EXTRAVER}/misc
cp filesystem/hcw/linux-${KERNELVER}/*.o ${MODDIR}/${KERNELVER}${EXTRAVER}/misc
mkdir -p filesystem/install/memory
mkdir -p filesystem/install/union

#
# Create the ramdisk out of the fs directory
#
if [ -a $RAMDISK ] ; then
	rm -rf $RAMDISK
fi
../tools/squashfs/squashfs2.2-r2/squashfs-tools/mksquashfs filesystem/install ${RAMDISK} -be -all-root -if filesystem/devtable || error "mksquashfs failed"

if [ "$KERNELVER" = "2.4.31" ] ; then
	#
	# The squashfs size is limited to the amount allocated by the linux
	# kernel in sdram bank1 (0xa0d00000-0xa0f00000).
	#
	SIZE=`stat -c %s ${RAMDISK}`
	echo "squashfs filesystem is $SIZE bytes"
	if [ $SIZE -gt 2097152 ] ; then
		echo "squashfs filesystem exceeds the limit of 2097152 bytes!"
		exit 1
	fi
fi

if [ "$KERNELVER" = "2.4.31" ] ; then
    cp ${RAMDISK} filesystem/install_wrapper/etc/rootfs.img
    rm -f ${RAMDISK}
    ../tools/squashfs/squashfs2.2-r2/squashfs-tools/mksquashfs filesystem/install_wrapper ${RAMDISK} -be -all-root -if filesystem/devtable || error "mksquashfs failed"
fi

make_dongle filesystem/kernel_files/vmlinux.gz ${RAMDISK}

rm -f ${RAMDISK}
rm -rf $TMP
#rm boot_loader

echo "Success"

exit 0
