#!/bin/sh

{ echo PVDID_CONNECTION_PROMOTE_CONTROL; echo PVDID_REMOVE_PVDID pvd.cisco.com; } | nc 0.0.0.0 10101
