#!/bin/sh

. `dirname $0`/check-nc.sh

{
	echo PVD_GET_ATTRIBUTES '*'
	echo PVD_SUBSCRIBE '*'
	echo PVD_SUBSCRIBE_NOTIFICATIONS
	while true; do sleep 1000; done
} | $NC 0.0.0.0 10101
