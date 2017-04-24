#!/bin/sh

{
	echo PVDID_GET_ATTRIBUTES pvd.cisco.com
	echo PVDID_SUBSCRIBE pvd.cisco.co
	echo PVDID_SUBSCRIBE_NOTIFICATIONS
	sleep 1000
} | nc 0.0.0.0 10101
