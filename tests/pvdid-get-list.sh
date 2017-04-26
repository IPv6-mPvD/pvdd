#!/bin/sh

echo PVDID_GET_LIST |
nc 0.0.0.0 10101 |
sed -e 's/PVDID_LIST //'
