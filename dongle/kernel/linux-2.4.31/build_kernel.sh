#!/bin/sh
#
# build a linux kernel
#

set -ex

TARBALLS_DIR=$1
CROSS=$2

rm -rf linux-2.4.31
rm -rf unionfs-1.0.14
rm -rf ac3
rm -rf mvpstb
rm -rf fuse/fuse-2.5.3

mkdir -p $TARBALLS_DIR
if [[ ! -a $TARBALLS_DIR/linux-2.4.31.tar.bz2 ]] ; then
    wget -O $TARBALLS_DIR/linux-2.4.31.tar.bz2 http://www.mvpmc.org/dl/linux-2.4.31.tar.bz2
fi
if [[ ! -a $TARBALLS_DIR/unionfs-1.0.14.tar.gz ]] ; then
    wget -O $TARBALLS_DIR/unionfs-1.0.14.tar.gz http://www.mvpmc.org/dl/unionfs-1.0.14.tar.gz
fi
tar --bzip2 -xf $TARBALLS_DIR/linux-2.4.31.tar.bz2
tar -xzf $TARBALLS_DIR/unionfs-1.0.14.tar.gz

PATCHES="architecture.patch squashfs2.2-patch smc91111.patch ppc405-wdt.patch kexec-ppc-2.4.patch mvp-version.patch clockfix.patch sdram-bank1.patch memsize.patch"

cp patches/dongle_version.h linux-2.4.31/include
cp patches/redwood.c linux-2.4.31/drivers/mtd/maps
cp patches/kernel_dot_config linux-2.4.31/.config

for i in $PATCHES ; do
    patch -f -N -s -p1 -d linux-2.4.31 < patches/$i
done

patch -p1 -d linux-2.4.31 < patches/misc-embedded.patch
patch -p1 -d linux-2.4.31 < patches/rd_size.patch
patch -p1 -d linux-2.4.31 < patches/stack_size.patch
patch -p1 -d linux-2.4.31 < patches/panic_timeout.patch

cd linux-2.4.31
patch -p1 < ../patches/cifs_24.patch
tar -xzf ../patches/fs_cifs.tgz
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

mkdir ac3
cd ac3
cp ../patches/ac3.c .
cp ../patches/ppc_40x.h .
${CROSS}gcc -D__KERNEL__ -I../linux-2.4.31/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -I../linux-2.4.31/arch/ppc -fsigned-char -msoft-float -pipe -ffixed-r2 -Wno-uninitialized -mmultiple -mstring -Wa,-m405 -DMODULE -c -o ac3_mod.o ac3.c
cp ac3_mod.o ../../../filesystem/tree/lib/modules/2.4.31-v1.1-hcwmvp/misc
cd ..

mkdir mvpstb
cd mvpstb
cp ../patches/mvpstb_mod.[ch] .
${CROSS}gcc -D__KERNEL__ -I../linux-2.4.31/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -I../linux-2.4.31/arch/ppc -fsigned-char -msoft-float -pipe -ffixed-r2 -Wno-uninitialized -mmultiple -mstring -Wa,-m405 -DMODULE -c -o mvpstb_mod.o mvpstb_mod.c
cp mvpstb_mod.o ../../../filesystem/tree/lib/modules/2.4.31-v1.1-hcwmvp/misc
cd ..
