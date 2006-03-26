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
wget -c -P $TARBALLS_DIR kegel.com/crosstool/crosstool-0.42.tar.gz
rm -rf crosstool-0.42
tar -xzf $TARBALLS_DIR/crosstool-0.42.tar.gz
cd crosstool-0.42

RESULT_TOP=/opt/crosstool
mkdir -p $RESULT_TOP
export TARBALLS_DIR RESULT_TOP
GCC_LANGUAGES="c,c++"
export GCC_LANGUAGES

#eval `cat gcc-3.4.1-glibc-2.3.3.dat powerpc-405.dat` sh all.sh
eval `cat gcc-3.2.3-glibc-2.2.5.dat powerpc-405.dat` sh all.sh

exit 0
