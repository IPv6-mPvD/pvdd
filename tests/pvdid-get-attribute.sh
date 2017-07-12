#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 2 ]
then
	echo "usage : $0 pvdId attrName"
	exit 1
fi

echo PVDID_GET_ATTRIBUTE "$@" | $NC 0.0.0.0 10101 | grep -v PVDID_ | jq -M --tab
