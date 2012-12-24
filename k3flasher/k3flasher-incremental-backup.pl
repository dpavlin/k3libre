#!/usr/bin/perl
use warnings;
use strict;

# replacement for k3-backup.sh which can continue transfer

while(<DATA>) {
	chomp;
	warn "# $_\n";
	my ( undef, undef, undef, $from, $length, $name ) = split(/\s+/, $_);

	$from   = hex($from);
	$length = hex($length);

	my $file_size = -s $name;

	printf "0x%08x 0x%08x %s\n", $from, $length, $name;

	if ( $file_size < $length ) {
		my $start_at = $length - $file_size + $from;
		my $cmd = sprintf "../k3flasher ../mx35to2_mmc.bin dump 0x%08x 0x%08x %s.0x%08x", $start_at, $length - $file_size, $name, $start_at;
		warn "## $cmd\n";
		system( $cmd ) == 0 || die "$!";
	} elsif ( $file_size == $length ) {
		warn "## $name COMPLETE\n";
	}

}

__DATA__
./k3flasher mx35to2_mmc.bin info
./k3flasher mx35to2_mmc.bin dump 0x0 0x00040c00 partitiontable-header-uboot.img
./k3flasher mx35to2_mmc.bin dump 0x00040c00 0x00000400 devid.img
./k3flasher mx35to2_mmc.bin dump 0x00041000 0x00340000 kernel.img
./k3flasher mx35to2_mmc.bin dump 0x00381000 0x00040000 isis.img
./k3flasher mx35to2_mmc.bin dump 0x003c1000 0x28a07000 rootfs.img
./k3flasher mx35to2_mmc.bin dump 0x28dc8000 0x01800000 varfs.img
./k3flasher mx35to2_mmc.bin dump 0x2a5c8000 0x00800000 partition3.img
