#!/bin/sh
#
# build a linux kernel
#

set -ex

TARBALLS_DIR=$1
CROSS=$2

rm -rf mvpdist
rm -rf unionfs-1.0.14
rm -rf ac3

mkdir -p $TARBALLS_DIR
if [[ ! -a $TARBALLS_DIR/mvpdist1.1.21315.tgz ]] ; then
    wget -O $TARBALLS_DIR/mvpdist1.1.21315.tgz http://www.mvpmc.org/dl/mvpdist1.1.21315.tgz
fi
if [[ ! -a $TARBALLS_DIR/unionfs-1.0.14.tar.gz ]] ; then
    wget -O $TARBALLS_DIR/unionfs-1.0.14.tar.gz http://www.mvpmc.org/dl/unionfs-1.0.14.tar.gz
fi
tar -xzf $TARBALLS_DIR/mvpdist1.1.21315.tgz
tar -xzf $TARBALLS_DIR/unionfs-1.0.14.tar.gz

cd mvpdist/kernel

patch -p1 < ../../patches/cifs_24.patch
patch -p2 < ../../patches/mvpstb_mod.patch
patch -p2 < ../../patches/printk.c.patch
patch -p2 < ../../patches/hcwmvp_header.patch
patch -p2 < ../../patches/squashfs_2.2r2.patch
rm include/linux/zlib.h lib/Config.in
patch -p1 < ../../patches/zlib.patch

tar -xzf ../../patches/fs_cifs.tgz
tar -xzf ../../patches/mvpstb.tar.gz -C drivers

echo $CROSS > .hhl_cross_compile
make distclean

cp ../../patches/kernel_dot_config .config
make oldconfig
make dep
make zImage
make modules

../../../../filesystem/kernel_copy.sh `pwd` ../../../../filesystem/kernel_files
cp drivers/mvpstb/mvpstb_mod.o ../../../../filesystem/tree/lib/modules/2.4.17_mvl21-vdongle/misc

cd ../..
cd unionfs-1.0.14
patch -p1 < ../patches/unionfs-1.0.14.patch
CROSS=$CROSS make unionfs2.4
cp unionfs.o ../../../filesystem/tree/lib/modules/2.4.17_mvl21-vdongle/misc
cd ..

mkdir ac3
cd ac3
cp ../patches/ac3.c .
${CROSS}gcc -D__KERNEL__ -I../mvpdist/kernel/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -I../mvpdist/kernel/arch/ppc -fsigned-char -msoft-float -pipe -ffixed-r2 -Wno-uninitialized -mmultiple -mstring -Wa,-m405 -DMODULE -c -o ac3_mod.o ac3.c
cp ac3_mod.o ../../../filesystem/tree/lib/modules/2.4.17_mvl21-vdongle/misc
cd ..
