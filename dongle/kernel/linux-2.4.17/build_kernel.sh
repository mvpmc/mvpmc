#!/bin/sh
#
# $Id$
#

set -ex

TARBALLS_DIR=$1
CROSS=$2

rm -rf mvpdist

mkdir -p $TARBALLS_DIR
if [ ! -a $TARBALLS_DIR/mvpdist1.1.21315.tgz ] ; then
    tar -xzf $TARBALLS_DIR/mvpdist1.1.21315.tgz
fi

cd mvpdist/kernel

patch -p1 < ../../patches/cifs_24.patch
patch -p2 < ../../patches/mvpstb_mod.patch
patch -p2 < ../../patches/printk.c.patch

tar -xzf ../../patches/fs_cifs.tgz
tar -xzf ../../patches/mvpstb.tar.gz
mv mvpstb drivers
cp ../../patches/kernel_dot_config .config

echo $CROSS > .hhl_cross_compile
make distclean

cp ../../patches/kernel_dot_config .config
make oldconfig
make dep
make zImage

../../../../filesystem/kernel_copy.sh `pwd` ../../../../filesystem/kernel_files
