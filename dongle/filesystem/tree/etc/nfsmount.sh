#!/bin/sh
#
# $Id$
#

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

mount -t nfs -o nolock,rsize=4096,wsize=4096 $SERVER $MNT
