#!/bin/sh

source `dirname $0`/check-nc.sh

echo PVDID_GET_LIST |
$NC 0.0.0.0 10101 |
sed -e 's/PVDID_LIST //'
