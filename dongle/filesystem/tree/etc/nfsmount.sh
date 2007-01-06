#!/bin/sh

KERNVER=`/bin/uname -r`

SERVER=$1
MNT=$2

if [ "$SERVER" = "" ] ; then
    echo "Usage: $0 <server> <mount point>"
    exit 1
fi
if [ "$MNT" = "" ] ; then
    echo "Usage: $0 <server> <mount point>"
    exit 1
fi

if [ "$KERNVER" = "2.4.17_mvl21-vdongle" ] ; then
    RSIZE=4096
else
    RSIZE=2048
fi

mount -t nfs -o nolock,rsize=${RSIZE},wsize=4096 $SERVER $MNT
