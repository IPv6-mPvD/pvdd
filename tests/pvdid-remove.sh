#!/bin/sh

. `dirname $0`/check-nc.sh

{ echo PVDID_CONNECTION_PROMOTE_CONTROL; echo PVDID_REMOVE_PVDID pvd.cisco.com; } | $NC 0.0.0.0 10101
