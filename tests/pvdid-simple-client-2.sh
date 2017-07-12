#!/bin/sh

source `dirname $0`/check-nc.sh

{
	echo PVDID_GET_ATTRIBUTES '*'
	echo PVDID_SUBSCRIBE '*'
	echo PVDID_SUBSCRIBE_NOTIFICATIONS
	while true; do sleep 1000; done
} | $NC 0.0.0.0 10101
