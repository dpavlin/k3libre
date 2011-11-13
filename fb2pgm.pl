#!/usr/bin/perl
use warnings;
use strict;

my $path = shift @ARGV || die "usage: $0 /dev/fb0";

open(my $in, '<', $path) || die "$path: $!";

sub gray {
	my $b = shift;
	return $b | $b << 4 ^ 0xff;
}

open(my $out, '>', "$path.pgm");

print $out "P5
824 1200
255
";


while(my $size = read($in, my $c, 300)) {

	last if $size < 300;

	foreach my $byte ( unpack 'C*', $c ) {
		my $a = $byte & 0xf0 >> 4;
		my $b = $byte & 0x0f;
		print $out pack 'CC', gray($a), gray($b);
	}
	print STDERR tell($in)," $size\n";
}

