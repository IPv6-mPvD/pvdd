#!/bin/sh

source `dirname $0`/check-nc.sh

{
	echo PVDID_GET_ATTRIBUTES pvd.cisco.com
	echo PVDID_SUBSCRIBE pvd.cisco.com
	echo PVDID_SUBSCRIBE_NOTIFICATIONS
	sleep 1000
} | $NC 0.0.0.0 10101
