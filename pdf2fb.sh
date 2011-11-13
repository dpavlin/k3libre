#!/bin/sh -xe

pdf=~/Downloads/pdf/NanoBSD_and_ALIX_BSD_06_2011.pdf
#pdf=/mnt/llin/rest/references/books/pdf/ggplot2-book.pdf
pdf=~/Downloads/pdf/ebooksclub.org__The_Art_of_Electronics_2nd_Ed_.pdf
page=$1
shift

test -z "$page" && page=1

# +1 besause expr 10 - 10 fails
export O_X=101
export O_Y=51

RES=150
GAMMA=1

while true ; do

./pdfdraw -o page-%d.pgm -r $RES -b 1 -g -m -G $GAMMA $* $pdf $page
echo $O_X $O_Y
./pgm2fb.pl page-$page.pgm | tee fb | nc 192.168.2.2 8888

key=`nc -l 8888`
case "$key" in

	124*) # Right>
		page=`expr $page + 1` ;;
	109*) # Right<
		page=`expr $page - 1` ;;

	# fiveway DXG
	122*) # Up
		O_Y=`expr $O_Y - 50` ;;
	123*) # Down
		O_Y=`expr $O_Y + 50` ;;
	105*) # Up
		O_X=`expr $O_X - 50` ;;
	106*) # Down
		O_X=`expr $O_X + 50` ;;

	114*) # Vol-
		GAMMA=`expr $GAMMA - 1` ;;
	115*) # Vol+
		GAMMA=`expr $GAMMA + 1` ;;

	139*) # Menu
		RES=`expr $RES + 10` ;;
	91*) # Back
		RES=`expr $RES - 10` ;;

	esac

done
