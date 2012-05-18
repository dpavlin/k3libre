#!/bin/sh -xe

name=$1
test -z "$name" && name=/tmp/kindle

landscape=`ssh root@192.168.2.2 cat /sys/module/eink_fb_hal_broads/parameters/bs_orientation`
ssh root@k dd if=/dev/fb0 > $name.fb

ls -al $name.fb

./fb2pgm.pl $name.fb $landscape && display $name.fb.pgm
