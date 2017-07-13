#!/bin/sh

. `dirname $0`/check-nc.sh

{
	echo PVD_CONNECTION_PROMOTE_CONTROL
	echo PVD_BEGIN_TRANSACTION pvd.cisco.com
	echo PVD_SET_ATTRIBUTE pvd.cisco.com RDNSS '["8.8.8.8", "8.8.4.4", "8.8.2.2"]'
	echo PVD_SET_ATTRIBUTE pvd.cisco.com DNSSL '["orange.fr", "free.fr"]'
	echo PVD_END_TRANSACTION pvd.cisco.com
} | $NC 0.0.0.0 10101

