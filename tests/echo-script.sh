#!/bin/sh

# Test script for the sockonnekt utility.
#
# To be used as follows :
# 1) start an echo server :
#	./echo.js >/dev/null 2>&1 &
# 2) run the script via sockonnekt :
#	.../sockonnekt -r -p 8124 -s 0.0.0.0 -- ./echo-script.sh
# 3) stop the echo server :
#	fg; CTRL-C


echo HELLO
i=0
while [ $i -ne 100 ]
do
	timeout 2 /bin/sh -c 'read j; echo $j 1>&2'
	echo HELLO $i
	i=$((i + 1))
	sleep 1
done
