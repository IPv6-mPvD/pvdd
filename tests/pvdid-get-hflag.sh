#!/bin/sh

. `dirname $0`/check-nc.sh

if [ $# != 1 ]
then
	echo "usage : $0 pvdname"
	exit 1
fi

echo PVD_GET_ATTRIBUTES "$@" |
$NC 0.0.0.0 10101 |
awk '/hFlag/ { print $3 }' |
sed -e 's/,//'
