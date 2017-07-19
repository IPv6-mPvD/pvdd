#!/bin/sh

. `dirname $0`/check-nc.sh

{ echo PVD_CONNECTION_PROMOTE_CONTROL; echo PVD_CREATE_PVD 100 smart.mpvd.io; } | $NC 0.0.0.0 10101
