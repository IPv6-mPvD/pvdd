#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 1 ]
then
	echo "usage : $0 pvdname"
	exit 1
fi

{
	echo PVD_CONNECTION_PROMOTE_CONTROL
	echo PVD_BEGIN_TRANSACTION "$@"
	echo PVD_SET_ATTRIBUTE "$@" sequenceNumber 1
	echo PVD_END_TRANSACTION "$@"
} | $NC 0.0.0.0 10101

