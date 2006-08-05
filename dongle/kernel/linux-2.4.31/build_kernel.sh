#!/bin/sh
#
# build a linux kernel
#

set -ex

TARBALLS_DIR=$1
CROSS=$2

rm -rf linux-2.4.31

mkdir -p $TARBALLS_DIR
if [[ ! -a $TARBALLS_DIR/linux-2.4.31.tar.bz2 ]] ; then
    wget -O $TARBALLS_DIR/linux-2.4.31.tar.bz2 http://www.kernel.org/pub/linux/kernel/v2.4/linux-2.4.31.tar.bz2
fi
if [[ ! -a $TARBALLS_DIR/unionfs-1.0.14.tar.gz ]] ; then
    wget -O $TARBALLS_DIR/unionfs-1.0.14.tar.gz ftp://ftp.fsl.cs.sunysb.edu/pub/unionfs/unionfs-1.0.14.tar.gz
fi
tar --bzip2 -xf $TARBALLS_DIR/linux-2.4.31.tar.bz2
tar -xzf $TARBALLS_DIR/unionfs-1.0.14.tar.gz

PATCHES="architecture.patch squashfs2.2-patch smc91111.patch ppc405-wdt.patch kexec-ppc-2.4.patch mvp-version.patch clockfix.patch sdram-bank1.patch memsize.patch"

cp patches/dongle_version.h linux-2.4.31/include
cp patches/redwood.c linux-2.4.31/drivers/mtd/maps
cp patches/config.production linux-2.4.31/.config

for i in $PATCHES ; do
    patch -f -N -s -p1 -d linux-2.4.31 < patches/$i
done

cd linux-2.4.31
patch -p0 < ../patches/ver.patch

export CROSS

make oldconfig
make dep
make zImage
make modules

../../../filesystem/kernel_copy.sh `pwd` ../../../filesystem/kernel_files

cd ..
cd unionfs-1.0.14
patch -p1 < ../patches/unionfs-1.0.14.patch
CROSS=$CROSS make unionfs2.4
cp unionfs.o ../../../filesystem/tree/lib/modules/2.4.31-v1.1-hcwmvp/misc
cd ..
