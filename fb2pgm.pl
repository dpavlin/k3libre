#!/usr/bin/perl
use warnings;
use strict;

my $path = shift @ARGV || die "usage: $0 /dev/fb0";

print "P5
824 1200
255
";

open(my $in, '<', $path) || die "$path: $!";

while(my $size = read($in, my $c, 300)) {

	foreach my $byte ( unpack 'C*', $c ) {
		my $a = $byte & 0xf0;
		my $b = $byte & 0x0f << 4;
		print pack 'CC', $a ^ 0xff, $b ^ 0xff;
	}
	print STDERR tell($in)," $size\n";
}

