#!/bin/sh

KERNVER=`/bin/uname -r`

if [ "$KERNVER" = "2.4.17_mvl21-vdongle" ] ; then
    RSIZE=4096
else
    RSIZE=2048
fi

if [ "$1" = "-udp" ] ; then
    PROTO=udp
    shift
else
    PROTO=tcp
    RSIZE=4096
fi

SERVER=$1
MNT=$2

if [ "$SERVER" = "" ] ; then
    echo "Usage: $0 [-udp] <server> <mount point>"
    exit 1
fi
if [ "$MNT" = "" ] ; then
    echo "Usage: $0 [-udp] <server> <mount point>"
    exit 1
fi

mount -t nfs -o nolock,${PROTO},rsize=${RSIZE},wsize=4096 $SERVER $MNT
