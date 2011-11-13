#!/usr/bin/perl
use warnings;
use strict;

my $path = shift @ARGV || die "usage: $0 image.pgm";

my $o_x = $ENV{O_X} || 0;
my $o_y = $ENV{O_Y} || 0;

open(my $in, '<', $path) || die "$path: $!";

my $magic = <$in>;
die "wrong magic $magic" unless $magic =~ m/^P5/;
my $size = <$in>;
chomp $size;
my ( $w, $h ) = split(/ /,$size);
<$in>;

my $fb;

my $y = 0;

seek($in, $w * $o_y, 1) if $o_y;

while(my $size = read($in, my $px, $w)) {

	foreach my $x ( 0 .. 824 / 2 - 1 ) {

		my ($a,$b);

		if ( length($px) < $x * 2 + $o_x + 2 ) {
			print "\x00";
		} else {	

			my ($a,$b) = unpack('CC', substr($px,$x * 2 + $o_x,2));
			print pack( 'C', (
				( $a & 0xf0 )
				|
				( ( $b & 0xf0 ) >> 4 )
			) ^ 0xff );

		}

	}

	$y++;
	last if $y == 1200;
}

