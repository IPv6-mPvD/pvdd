#!/bin/bash

# usage : pvdid-monitor.sh <pvdid>*

ALLPVDIDS="$@"
for i in $ALLPVDIDS
do
	$V[$i]=""
done

send_trigger() {
	echo "PVD $1 UPDATED" >$PIPE
}

while true
do
	sleep 60
	for i in $ALLPVDIDS
	do
		# wget https://"$i"/pvd.json -O/tmp/pvd-"$i".json
		wget https://"$i"/pvd.json -O/tmp/pvd-"$i".json
		json=`cat /tmp/pvd-"$i".json`
		if [ "$json" != "$V[$i]" ]
		then
			send_trigger "$i"
		fi
	done
done
