#!/bin/sh

{ echo PVDID_CONNECTION_PROMOTE_CONTROL; echo PVDID_CREATE_PVDID 100 pvd.cisco.com; } | nc 0.0.0.0 10101
