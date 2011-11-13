pdf=~/Downloads/pdf/NanoBSD_and_ALIX_BSD_06_2011.pdf
pdf=/mnt/llin/rest/references/books/pdf/ggplot2-book.pdf
page=$1

test -z "$page" && page=1

./pdfdraw -o page-%d.pgm -r 150 -b 1 -g -m $pdf $page
./pgm2fb.pl page-$page.pgm | tee fb | nc 192.168.2.2 8888

