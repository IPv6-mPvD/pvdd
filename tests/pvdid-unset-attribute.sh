#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 2 ]
then
	echo "usage : $0 pvdname attrName"
	exit 1
fi

{
	echo PVD_CONNECTION_PROMOTE_CONTROL
	echo PVD_UNSET_ATTRIBUTE "$@"
} | $NC 0.0.0.0 10101

