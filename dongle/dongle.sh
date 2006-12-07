#!/bin/sh
#
# Build the dongle.bin file
#

set -e

STRIP=${CROSS}strip
TOOLLIB=`dirname ${CROSS}`/../lib

echo STRIP $STRIP

DIRS="bin sbin usr/bin usr/sbin lib dev proc var usr/share usr/share/mvpmc usr/share/udhcpc etc tmp oldroot"
WRAPPER_DIRS="bin sbin etc usr/bin usr/sbin dev tmp lib proc mnt"

BIN="busybox mvpmc ntpclient scp ticonfig"
WRAPPER_BIN="busybox splash"

SBIN="dropbearmulti dropbear dropbearkey"

USRBIN=""

USRSBIN=""

LIB="libav.so libcmyth.so libdemux.so libosd.so libts_demux.so libvnc.so libwidget.so libvorbisidec.so.1.0.2 libvorbisidec.so.1"
TLIB="libc.so.0 libm.so.0 libcrypt.so.0 libgcc_s_nof.so.1 libpthread.so.0 libutil.so.0"
LDLIB="ld-uClibc-0.9.28.so ld-uClibc.so.0"

WRAPPERLIB="libc.so.0 libcrypt.so.0 libgcc_s_nof.so.1"

rm -rf filesystem/install
rm -rf filesystem/install_wrapper

for i in $DIRS ; do
    mkdir -p filesystem/install/$i
done
for i in $WRAPPER_DIRS ; do
    mkdir -p filesystem/install_wrapper/$i
done

cd filesystem/tree
tar -cf - * | tar -xf - -C ../install
cd ../..
cd filesystem/wrapper
tar -cf - * | tar -xf - -C ../install_wrapper
cd ../..

for i in $LIB ; do
    cp -d install/mvp/lib/$i filesystem/install/lib
    $STRIP filesystem/install/lib/$i
done
for i in $TLIB ; do
    cp $TOOLLIB/$i filesystem/install/lib
    $STRIP filesystem/install/lib/$i
done
for i in $LDLIB ; do
    cp -d $TOOLLIB/$i filesystem/install/lib
    cp -d $TOOLLIB/$i filesystem/install_wrapper/lib
done
for i in $WRAPPERLIB ; do
    cp $TOOLLIB/$i filesystem/install_wrapper/lib
    $STRIP filesystem/install_wrapper/lib/$i
done

for i in $BIN ; do
    cp -d install/mvp/bin/$i filesystem/install/bin
    $STRIP filesystem/install/bin/$i
done
for i in $WRAPPER_BIN ; do
    cp -d install/mvp/bin/$i filesystem/install_wrapper/bin
    $STRIP filesystem/install_wrapper/bin/$i
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

cp $TOOLLIB/../powerpc-405-linux-uclibc/target_utils/ldd filesystem/install/usr/bin

awk -F/ '{if(/^\/bin\/[^\/]+$/) { system("ln -s busybox filesystem/install" $0 ) } else {rp=sprintf("%" NF-2 "s", ""); gsub(/./,"../",rp); system("ln -sf " rp "bin/busybox filesystem/install" $0) }}' apps/busybox/mvp/busybox-*/busybox.links
awk -F/ '{if(/^\/bin\/[^\/]+$/) { system("ln -s busybox filesystem/install_wrapper" $0 ) } else {rp=sprintf("%" NF-2 "s", ""); gsub(/./,"../",rp); system("ln -sf " rp "bin/busybox filesystem/install_wrapper" $0) }}' apps/busybox/mvp/busybox-*/busybox.links

cp -d install/mvp/usr/share/mvpmc/* filesystem/install/usr/share/mvpmc
cp -d install/mvp/linuxrc filesystem/install

cp -d install/mvp/linuxrc filesystem/install_wrapper

#filesystem/kernel_copy.sh kernel/linux-2.4.17/mvpdist/kernel filesystem/kernel_files

find filesystem/install -name .svn | xargs rm -rf
find filesystem/install -name .gitmo | xargs rm -rf

filesystem/dongle_build.sh -o ../dongle.bin.mvpmc -k filesystem/kernel_files

dd if=../dongle.bin.mvpmc of=../dongle.bin.mvpmc.ver bs=1 count=40 skip=52

