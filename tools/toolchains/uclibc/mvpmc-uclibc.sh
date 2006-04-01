#!/bin/sh
# Strange little script to demonstrate building a uclibc-based toolchain
# using an old version of crosstool
#
# Edited for use with mvpmc.
# http://mvpmc.sourceforge.net/
#

if [ "$1" != "" ] ; then
    GCC_PATH=$1
fi

#
# Newer versions of gcc cannot build this toolchain
#
unset CC
if [ -x /usr/bin/gcc32 ] ; then
    export CC=/usr/bin/gcc32
fi

set -ex
TARBALLS_DIR=$HOME/downloads
mkdir -p $TARBALLS_DIR
wget -c -P $TARBALLS_DIR kegel.com/crosstool/crosstool-0.28-rc5.tar.gz
rm -rf crosstool-0.28-rc5
tar -xzf $TARBALLS_DIR/crosstool-0.28-rc5.tar.gz
cd crosstool-0.28-rc5
patch -p1 < ../crosstool-uclibc-0.28-rc5-ter.patch

sed 's/linux-gnu/linux-uclibc/' < powerpc-405.dat > powerpc-405-uclibc.dat

DIR=`pwd`
echo UCLIBCCONFIG=$DIR/../uClibc.config >> gcc-3.3.3-uclibc-0.9.23.dat

mkdir -p patches/uClibc-0.9.23
cp ../setjmp.patch patches/uClibc-0.9.23

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

# turn off shell exit since we know the next command will fail
set +e

eval `cat gcc-3.3.3-uclibc-0.9.23.dat powerpc-405-uclibc.dat` sh all.sh

# The above command fails the "hello, world" test when linking non-static c++ programs,
# with error
#  mipsel-unknown-linux-uclibc-g++ hello2.cc -o mipsel-unknown-linux-uclibc-hello2
#  mipsel-unknown-linux-uclibc/lib/libstdc++.so: undefined reference to `sqrtf
# but can link static ones ok. 

# If we know where the cross-compiler should end up, fail if it is not there
if [ "$GCC_PATH" != "" ] ; then
    if [ -x $GCC_PATH ] ; then
	exit 0
    else
	exit 1
    fi
fi

exit 0

