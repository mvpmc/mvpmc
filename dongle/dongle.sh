#!/bin/sh
#
# Build the dongle.bin file
#

set -ex

STRIP=${CROSS}strip

echo STRIP $STRIP

DIRS="bin sbin usr/bin usr/sbin lib dev proc var usr/share usr/share/mvpmc usr/share/udhcpc etc tmp"

BIN="busybox mvpmc ntpclient"

SBIN=""

USRBIN=""

USRSBIN=""

rm -rf filesystem/install

for i in $DIRS ; do
    mkdir -p filesystem/install/$i
done

cd filesystem/tree
tar -cf - * | tar -xf - -C ../install
cd ../..

for i in $BIN ; do
    cp -d install/mvp/bin/$i filesystem/install/bin
    $STRIP filesystem/install/bin/$i
done

for i in $SBIN ; do
    cp -d install/mvp/sbin/$i filesystem/install/sbin
    $STRIP filesystem/install/sbin/$i
done

for i in $USRBIN ; do
    cp -d install/mvp/usr/bin/$i filesystem/install/usr/bin
    $STRIP filesystem/install/usr/bin/$i
done

for i in $USRSBIN ; do
    cp -d install/mvp/usr/sbin/$i filesystem/install/usr/sbin
    $STRIP filesystem/install/usr/sbin/$i
done

awk -F/ '{if(/^\/bin\/[^\/]+$/) { system("ln -s busybox filesystem/install" $0 ) } else {rp=sprintf("%" NF-2 "s", ""); gsub(/./,"../",rp); system("ln -sf " rp "bin/busybox filesystem/install" $0) }}' apps/busybox/mvp/busybox-*/busybox.links

cp -d install/mvp/usr/share/mvpmc/* filesystem/install/usr/share/mvpmc
cp -d install/mvp/linuxrc filesystem/install

#filesystem/kernel_copy.sh kernel/linux-2.4.17/mvpdist/kernel filesystem/kernel_files

find filesystem/install -name .svn | xargs rm -rf

filesystem/dongle_build.sh -o ../dongle.bin.mvpmc -k filesystem/kernel_files

dd if=../dongle.bin.mvpmc of=../dongle.bin.mvpmc.ver bs=1 count=40 skip=52

