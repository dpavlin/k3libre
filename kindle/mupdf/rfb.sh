#!/bin/sh

while true ; do
	date
	./netcat -l -p 8888 > /dev/fb0
	echo 1 > /proc/eink_fb/update_display
done
