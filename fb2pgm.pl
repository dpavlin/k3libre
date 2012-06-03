#!/usr/bin/perl
use warnings;
use strict;

my ($path,$landscape) = @ARGV;

die "usage: $0 /dev/fb0 [landscape]" unless $path;

my $size = -s $path;
my ( $w, $h ) = ( 824, 1200 ); # DXG
   ( $w, $h ) = ( 600, 800 ) if $size < $w * $h / 2;

( $h, $w ) = ( $w, $h ) if $landscape;

open(my $in, '<', $path) || die "$path: $!";
warn "# reading $path $w * $h\n";

sub gray {
	my $b = shift;
	return $b << 4 ^ 0xff;
}

open(my $out, '>', "$path.pgm");

print $out "P5
$w $h
255
";


while(my $size = read($in, my $c, $w / 2)) {

	last if $size < $w / 2;

	foreach my $byte ( unpack 'C*', $c ) {
		my $a = ( $byte & 0xf0 ) >> 4;
		my $b =   $byte & 0x0f;
		print $out pack 'CC', gray($a), gray($b);
	}
}

print "$path.pgm\n";
