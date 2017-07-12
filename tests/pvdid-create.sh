#!/bin/sh

source `dirname $0`/check-nc.sh

{ echo PVDID_CONNECTION_PROMOTE_CONTROL; echo PVDID_CREATE_PVDID 100 pvd.cisco.com; } | $NC 0.0.0.0 10101
