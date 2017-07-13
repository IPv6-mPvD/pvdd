#!/bin/sh

. `dirname $0`/check-nc.sh

{
	echo PVD_GET_ATTRIBUTES pvd.cisco.com
	echo PVD_SUBSCRIBE pvd.cisco.com
	echo PVD_SUBSCRIBE_NOTIFICATIONS
	sleep 1000
} | $NC 0.0.0.0 10101
