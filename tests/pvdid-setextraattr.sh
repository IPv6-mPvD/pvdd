#!/bin/sh

source `dirname $0`/check-nc.sh

{
	echo PVDID_CONNECTION_PROMOTE_CONTROL
	echo PVDID_BEGIN_TRANSACTION pvd.cisco.com
	echo PVDID_MULTILINE 5
	echo PVDID_SET_ATTRIBUTE pvd.cisco.com extraJson
	echo '{'
	echo '		"validUntil" : "2017-04-17T06:00:00Z",'
	echo '		"name" : "orange.fr"'
	echo '	}'
	echo PVDID_END_TRANSACTION pvd.cisco.com
} | $NC 0.0.0.0 10101

