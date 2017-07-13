#!/bin/sh

. `dirname $0`/check-nc.sh

{ echo PVD_CONNECTION_PROMOTE_CONTROL; echo PVD_REMOVE_PVD pvd.cisco.com; } | $NC 0.0.0.0 10101
