#!/bin/sh
#
# $Id$
#
# Copyright (C) 2004, Jon Gettler
# http://mvpmc.sourceforge.net/
#
# This script will copy the IBM kernel modules from a Hauppauge dongle.bin
# bootfile and build a new bootable mediamvp file from the supplied filesystem.
#
# The programs in bin were built under Suse 9.0.  If they do not work for you,
# you will need to rebuild them.  See http://www.dforsyth.net/mvp/software.html
# for info on setting up a cross compilation environment.
#

#set -x

TMP=`mktemp -q -d dir.XXXXXX` || error "mktemp dir failed"
RAMDISK=`mktemp -q ramdisk.XXXXXX` || error "mktemp dir failed"

print_help() {
	echo "$0 [-d dongle.bin] [-k kernel dir] [-o outfile]"
}

error() {
	echo "ERROR: $1"

	exit 1
}

#
# make_dongle() - this is straight out of arch/ppc in the linux tree
#
make_dongle() {
	KERN=$1
	RD=$2

	CROSS=bin/powerpc-405-linux-gnu-
	OBJCOPY=${CROSS}objcopy
	LD=${CROSS}ld

$OBJCOPY -O elf32-powerpc \
	--add-section=.ramdisk=${RD} \
	--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
	--add-section=.image=${KERN} \
	--set-section-flags=.image=contents,alloc,load,readonly,data \
	kernel_files/dummy.o ${TMP}/image.o
$LD -T kernel_files/ld.script -Ttext 0x00400000 -Bstatic -o ${TMP}/zvmlinux.initrd kernel_files/head.o kernel_files/relocate.o  kernel_files/misc-embedded.o kernel_files/misc-common.o kernel_files/string.o kernel_files/util.o kernel_files/embed_config.o kernel_files/ns16550.o ${TMP}/image.o kernel_files/zlib.a
$OBJCOPY -O elf32-powerpc ${TMP}/zvmlinux.initrd ${TMP}/zvmlinux.initrd -R .comment -R .stab -R .stabstr \
	-R .sysmap
./bin/mktree ${TMP}/zvmlinux.initrd ${OUTFILE}
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

if [[ "$DONGLE" = "" || "$OUTFILE" = "" ]] ; then
	print_help
	exit 1
fi

./dongle_split.pl $DONGLE || error "dongle split failed"
mv ramdisk ramdisk.gz || error "move failed"
gunzip ramdisk.gz
mv ramdisk vmlinux $TMP

#
# Copy the kernel libraries into the fs dir
#
mkdir -p fs/lib/modules/2.4.17_mvl21-vdongle/misc
/sbin/debugfs -f fs_cmd ${TMP}/ramdisk
cp -f /etc/localtime fs/etc

RAMDISK_SIZE=`du -ks fs | cut -f 1`
let RAMDISK_SIZE=$RAMDISK_SIZE+300
if [ $RAMDISK_SIZE -gt 4096 ] ; then
	error "ramdisk too big"
fi

#
# Create the ramdisk out of the fs directory
#
bin/genext2fs -d fs -b ${RAMDISK_SIZE} -f spec ${RAMDISK}

gzip $RAMDISK

make_dongle kernel_files/vmlinux.gz ${RAMDISK}.gz

rm -f ${RAMDISK}.gz
rm -rf $TMP
rm boot_loader

echo "Success"

exit 0
