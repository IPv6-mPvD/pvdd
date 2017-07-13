#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 1 ]
then
	echo "usage : $0 pvdname"
	exit 1
fi

{
	echo PVDID_CONNECTION_PROMOTE_CONTROL
	echo PVDID_BEGIN_TRANSACTION "$@"
	echo PVDID_SET_ATTRIBUTE "$@" hFlag 0
	echo PVDID_END_TRANSACTION "$@"
} | $NC 0.0.0.0 10101

