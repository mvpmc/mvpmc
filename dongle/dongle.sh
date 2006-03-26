#!/bin/sh
#
# $Id$
#
# Build the dongle.bin file
#

set -ex

STRIP=${CROSS}strip

echo STRIP $STRIP

DIRS="bin sbin usr/bin usr/sbin lib dev proc var usr/share usr/share/mvpmc usr/share/udhcpc etc tmp"

BIN="ash busybox cat chgrp chmod chown cp date dd df dmesg echo false fgrep grep hostname kill ln login ls mkdir mknod more mount msh mv netstat ping ps pwd rm rmdir sh sleep sync touch true umount uname mvpmc"

SBIN="adjtimex halt ifconfig init insmod klogd losetup lsmod modprobe mount.cifs poweroff reboot rmmod route syslogd udhcpc"

USRBIN="[ basename clear cut dirname du env find free ftpget ftpput head id killall logger mesg nslookup reset sort strings tail test tftp time top tty uniq uptime which whoami yes"

USRSBIN="chroot rdate telnetd"

rm -rf filesystem/install

for i in $DIRS ; do
    mkdir -p filesystem/install/$i
done

cd filesystem/tree
tar -cf - * | tar -xf - -C ../install
cd ../..

for i in $BIN ; do
    cp -d install/mvp/bin/$i filesystem/install/bin
done
$STRIP filesystem/install/bin/*

for i in $SBIN ; do
    cp -d install/mvp/sbin/$i filesystem/install/sbin
done
$STRIP filesystem/install/sbin/*

for i in $USRBIN ; do
    cp -d install/mvp/usr/bin/$i filesystem/install/usr/bin
done
$STRIP filesystem/install/usr/bin/*

for i in $USRSBIN ; do
    cp -d install/mvp/usr/sbin/$i filesystem/install/usr/sbin
done
$STRIP filesystem/install/usr/sbin/*

cp -d install/mvp/usr/share/mvpmc/* filesystem/install/usr/share/mvpmc
cp -d install/mvp/linuxrc filesystem/install

#filesystem/kernel_copy.sh kernel/linux-2.4.17/mvpdist/kernel filesystem/kernel_files

find filesystem/install -name .svn | xargs rm -rf

filesystem/dongle_build.sh -o ../dongle.bin.mvpmc -k filesystem/kernel_files
