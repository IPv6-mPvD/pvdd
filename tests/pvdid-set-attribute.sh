#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 3 ]
then
	echo "usage : $0 pvdname attrName attrValue"
	exit 1
fi

{
	echo PVD_CONNECTION_PROMOTE_CONTROL
	echo PVD_BEGIN_TRANSACTION "$1"
	echo PVD_SET_ATTRIBUTE "$@"
	echo PVD_END_TRANSACTION "$1"
} | $NC 0.0.0.0 10101

