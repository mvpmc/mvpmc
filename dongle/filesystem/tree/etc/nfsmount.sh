#!/bin/sh

if [ "$1" = "-udp" ] ; then
    PROTO=udp
    RSIZE=2048
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
