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

