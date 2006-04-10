#!/bin/sh

if [ "$1" != "" ] ; then
    GCC_PATH=$1
fi

unset CC
if [ -x /usr/bin/gcc32 ] ; then
    export CC=/usr/bin/gcc32
fi

set -ex
TARBALLS_DIR=$HOME/downloads
mkdir -p $TARBALLS_DIR
wget -c -P $TARBALLS_DIR kegel.com/crosstool/crosstool-0.25.tar.gz
wget -c -P $TARBALLS_DIR ftp://ftp.shspvr.com/download/mediamvp_download/crosstool-0.25-ppc405-gcc2.patch.gz
rm -rf crosstool-0.25
tar -xzf $TARBALLS_DIR/crosstool-0.25.tar.gz
cd crosstool-0.25

RESULT_TOP=/opt/crosstool
mkdir -p $RESULT_TOP
export TARBALLS_DIR RESULT_TOP
GCC_LANGUAGES="c,c++"
export GCC_LANGUAGES

# make sure the user can install the cross compiler
if [[ ! -w $RESULT_TOP ]] ; then
    echo "Cannot build the cross-compiler, since $RESULT_TOP is not writable!"
    exit 1
fi

#eval `cat gcc-3.4.1-glibc-2.3.3.dat powerpc-405.dat` sh all.sh
#eval `cat gcc-3.2.3-glibc-2.2.5.dat powerpc-405.dat` sh all.sh
#eval `cat gcc-2.95.3-glibc-2.2.5.dat powerpc-405.dat` sh all.sh

zcat $TARBALLS_DIR/crosstool-0.25-ppc405-gcc2.patch.gz |patch -p1
sh < ./mvp-ppc405.sh

exit 0
