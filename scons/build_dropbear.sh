#!/bin/bash
#
# $Id: build_app.sh 20 2006-03-14 05:26:56Z gettler $
#
# build_app.sh - build an application
#

help() {
    echo "build_app.sh [options]"
    echo "	-c CROSS   cross-compiler prefix"
    echo "	-d dir     application directory"
    echo "	-h         print this help"
    echo "	-i file    input file to build"
    echo "	-I dir     install directory"
    echo "	-p file    patch file (applied in order)"
    exit 0
}

PATCHES=
INPUT=
DIR=

while getopts "c:d:hi:I:p:" i
  do case "$i" in
      c) CROSS=$OPTARG;;
      d) DIR=$OPTARG;;
      h) help ;;
      i) INPUT=$OPTARG;;
      I) INSTALL=$OPTARG;;
      p) PATCHES="$PATCHES $OPTARG";;
      *) echo error ; exit 1 ;;
  esac
done

echo "PATCHES: $PATCHES"
echo "INPUT: $INPUT"
echo "DIR: $DIR"
echo "CROSS: $CROSS"
echo "INSTALL: $INSTALL"

if [ "$DIR" = "" ] ; then
    echo "Application directory not specified!"
    exit 1;
fi

if [ "$INPUT" = "" ] ; then
    echo "Application input file not specified!"
    exit 1
fi

if [ ! -f $INPUT ] ; then
    echo "Application input file does not exist!"
    exit 1
fi

if [ -d $DIR ] ; then
    rm -rf $DIR
fi

mkdir -p `dirname $DIR` || exit 1
cd `dirname $DIR` || exit 1

case "${INPUT##*.}" in
    gz|tgz) tar -xzf $INPUT ;;
    bz2)    tar --bzip2 -xf $INPUT ;;
    *)      echo unknown file type ; exit 1 ;;
esac

cd $DIR || exit 1

for i in $PATCHES ; do
    patch -p1 < ../../$i
done

export CROSS_PREFIX=$CROSS
export CC=${CROSS}gcc
export INSTALL=$INSTALL
export INSTALL_PREFIX=$INSTALL

if [ -f configure ] ; then
    ./configure --disable-zlib --prefix=$INSTALL --host=powerpc
    make clean
    make PROGRAMS="dropbear dropbearkey scp" MULTI=1
    mkdir -p $INSTALL/sbin
    cp dropbearmulti $INSTALL/sbin/dropbearmulti
    ln -sf ./dropbearmulti $INSTALL/sbin/dropbear
    ln -sf ./dropbearmulti $INSTALL/sbin/dropbearkey
    mkdir -p $INSTALL/bin
    ln -sf ../sbin/dropbearmulti $INSTALL/bin/scp
else
    make oldconfig
    make install
fi
