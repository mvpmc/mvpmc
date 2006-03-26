#!/bin/bash
#
# $Id: build_lib.sh 20 2006-03-14 05:26:56Z gettler $
#
# build_app.sh - build a library
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

while getopts "c:C:d:hi:I:p:" i
  do case "$i" in
      c) CROSS=$OPTARG;;
      C) CONFIG_OPTS=$OPTARG;;
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
echo "CONFIG_OPTS: $CONFIG_OPTS"

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
    patch -p1 < ../../$i || exit 1
done

export CROSS=$CROSS
export CROSS_PREFIX=$CROSS
export CC=${CROSS}gcc
export INSTALL_PREFIX=$INSTALL
export TOPLEVEL=$INSTALL

if [ -f configure ] ; then
    ./configure --prefix=$INSTALL $CONFIG_OPTS
    make
    make install
    if [ "`basename $PWD`" = "jpeg-6b" ] ; then
	make install-lib
    fi
else
    if [ -f autogen.sh ] ; then
	./autogen.sh --enable-shared --host powerpc-linux-gnu --prefix=$INSTALL
	make
	make install
    fi
    if [ "`basename $PWD`" = "microwindows-0.91" ] ; then
	if [ "$CC" = "gcc" ] ; then
#	    cp --remove-destination src/Configs/config.mvp.x11 src/config
	    echo no copy
	else
	    cp --remove-destination src/Configs/config.mvp src/config
	fi
	cd src
	make
	cd ..
	cp src/lib/*.a $INSTALL/lib
    fi
fi
