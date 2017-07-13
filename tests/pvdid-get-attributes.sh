#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 1 ]
then
	echo "usage : $0 pvdname"
	exit 1
fi

echo PVDID_GET_ATTRIBUTES "$@" | $NC 0.0.0.0 10101 | grep -v PVDID_ | jq -M --tab
