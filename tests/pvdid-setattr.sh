#!/bin/sh

. `dirname $0`/check-nc.sh

{
	echo PVDID_CONNECTION_PROMOTE_CONTROL
	echo PVDID_BEGIN_TRANSACTION pvd.cisco.com
	echo PVDID_SET_ATTRIBUTE pvd.cisco.com RDNSS '["8.8.8.8", "8.8.4.4", "8.8.2.2"]'
	echo PVDID_SET_ATTRIBUTE pvd.cisco.com DNSSL '["orange.fr", "free.fr"]'
	echo PVDID_END_TRANSACTION pvd.cisco.com
} | $NC 0.0.0.0 10101

