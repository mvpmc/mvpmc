#!/bin/sh
#
# $Id: dongle_build.sh,v 1.6 2006/02/10 13:49:47 stuart Exp $
#
# Copyright (C) 2004,2005 Jon Gettler
# http://mvpmc.sourceforge.net/
#
# This script will copy the IBM kernel modules from a Hauppauge dongle.bin
# bootfile and build a new bootable mediamvp file from the supplied filesystem.
#
# The programs in bin were built under Suse 9.0.  If they do not work for you,
# you will need to rebuild them.  See http://www.dforsyth.net/mvp/software.html
# or http://mvpmc.sourceforge.net/ for info on setting up a cross compilation
# environment.
#
# To use your own versions of mktree and/or genext2fs, make sure they can be
# found in $PATH.  For objcopy and ld, set $CROSS to point to their prefix.
#

set -ex

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
$LD -T filesystem/kernel_files/ld.script -Ttext 0x00400000 -Bstatic -o ${TMP}/zvmlinux.initrd filesystem/kernel_files/head.o filesystem/kernel_files/relocate.o  filesystem/kernel_files/misc-embedded.o filesystem/kernel_files/misc-common.o filesystem/kernel_files/string.o filesystem/kernel_files/util.o filesystem/kernel_files/embed_config.o filesystem/kernel_files/ns16550.o ${TMP}/image.o filesystem/kernel_files/zlib.a
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

#if [[ "$DONGLE" = "" || "$OUTFILE" = "" ]] ; then
#	print_help
#	exit 1
#fi

#./dongle_split.pl $DONGLE || error "dongle split failed"
#mv ramdisk ramdisk.gz || error "move failed"
#gunzip ramdisk.gz
#mv ramdisk vmlinux $TMP

#
# Copy the kernel libraries into the fs dir
#
#mkdir -p fs/lib/modules/2.4.17_mvl21-vdongle/misc
#/sbin/debugfs -f fs_cmd ${TMP}/ramdisk
#cp -f /etc/localtime fs/etc
mkdir -p filesystem/install/lib/modules/2.4.17_mvl21-vdongle/misc
cp filesystem/hcw/linux-2.4.17/*.o filesystem/install/lib/modules/2.4.17_mvl21-vdongle/misc

RAMDISK_SIZE=`du -ks filesystem/install | cut -f 1`
let RAMDISK_SIZE=$RAMDISK_SIZE+300
if [ $RAMDISK_SIZE -gt 4096 ] ; then
	error "ramdisk too big"
fi

#
# Create the ramdisk out of the fs directory
#
../tools/genext2fs/genext2fs-1.4rc1/genext2fs -d filesystem/install -b ${RAMDISK_SIZE} -D filesystem/devtable ${RAMDISK} || error "genext2fs failed"

gzip $RAMDISK

make_dongle filesystem/kernel_files/vmlinux.gz ${RAMDISK}.gz

rm -f ${RAMDISK}.gz
rm -rf $TMP
#rm boot_loader

echo "Success"

exit 0
