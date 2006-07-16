#!/bin/sh
# Simple script to kick off a buildroot system to build our cross-compiling
# toolchain

cd `dirname $0` || exit 1

wget -c -O $1 $2

rm -rf buildroot

( bunzip2 -c $1 | tar -xvf - ) || exit 2

patch -p0 < buildroot-20060710-mvpmc.patch || exit 3

cd buildroot || exit 4

make oldconfig || exit 5

make || exit 6

exit 0

