#!/bin/sh

{
	echo PVDID_GET_ATTRIBUTES '*'
	echo PVDID_SUBSCRIBE '*'
	echo PVDID_SUBSCRIBE_NOTIFICATIONS
	while true; do sleep 1000; done
} | nc 0.0.0.0 10101
