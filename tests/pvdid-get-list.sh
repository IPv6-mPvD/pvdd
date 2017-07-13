#!/bin/sh

. `dirname $0`/check-nc.sh

echo PVD_GET_LIST |
$NC 0.0.0.0 10101 |
sed -e 's/PVD_LIST //'
