#!/usr/bin/perl
use warnings;
use strict;
use autodie;

use Data::Dump qw(dump);

# http://code.google.com/p/kindle-annotations/wiki/PDRFileFormat


my $pdr = shift @ARGV;

open(my $f, '<', $pdr);

read($f, my $b, 4+ 1 + 4 + 4);
warn "# ",dump($b), "\n# unpack ", dump(unpack('A4CNN', $b)),$/;
my ( $header, undef, $last_displayed_page, $number_of_bookmarks ) = unpack 'A4CNN', $b;

print "last_displayed_page: $last_displayed_page\n";
print "number_of_bookmarks: $number_of_bookmarks\n";

die "header error", dump $header unless $header eq "\xDE\xAD\xCA\xBB";

sub string {
	read($f, my $len, 2);
	$len = unpack 'n', $len;
	read($f, my $str, $len);
	warn "# string [$len] ",dump($str).$/;
	return $str;
}

sub u8 {
	read($f, my $b, 1);
	warn "# u8 ",dump($b),$/;
	return unpack('C',$b);
}

sub u32 {
	read($f, my $b, 4);
	warn "# u32 ",dump($b),$/;
	return unpack('N',$b);
}

sub skip {
	my $len = shift;
	read($f, $b, $len);
	warn "# skip $len bytes ",dump($b),$/;
}

foreach my $nr ( 1 .. $number_of_bookmarks ) {
	print "bookmark $nr: ", u8, " page: ", u32, " name: ", string, $/;

}

skip 20;

my $number_of_markings = u32;
print "number_of_markings: $number_of_markings\n";

foreach my $nr ( 1 .. $number_of_markings ) {
	print "marking $nr: ", u8, " page:", u32, " name:",string, " loc1:", string;
	skip 4 + 8 + 8;
	print " -> page: ", u32, " name:",string, " loc2:",string;
	skip 4 + 8 + 8 + 2;

}
