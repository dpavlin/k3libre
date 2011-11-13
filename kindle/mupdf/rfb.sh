#!/bin/sh -xe

while true ; do
	./netcat -l -p 8888 > /dev/fb0
	echo 1 > /proc/eink_fb/update_display
	key=`waitforkey`
	echo $key | ./netcat -q 0 192.168.2.1 8888
done
