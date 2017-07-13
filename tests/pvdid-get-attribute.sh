#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 2 ]
then
	echo "usage : $0 pvdname attrName"
	exit 1
fi

echo PVD_GET_ATTRIBUTE "$@" | $NC 0.0.0.0 10101 | grep -v PVD_ | jq -M --tab
