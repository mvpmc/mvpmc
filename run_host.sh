#!/bin/sh
#
# Simple script to setup appropriate environment and run host build of mvpmc
#
STARTPATH=`dirname $0`/dongle/install/host
export LD_LIBRARY_PATH="$STARTPATH/lib:$LD_LIBRARY_PATH"
"$STARTPATH/bin/mvpmc" -i "$STARTPATH/usr/share/mvpmc" "$@"
