#!/bin/sh
echo "This will backup your K3."
echo "After starting this, bring your K3 into USB downloader mode."
echo "The whole backup procedure will take about 1 hour."

./k3flasher mx35to2_mmc.bin info
./k3flasher mx35to2_mmc.bin dump 0x0 0x00040c00 partitiontable-header-uboot.img
./k3flasher mx35to2_mmc.bin dump 0x00040c00 0x00000400 devid.img
./k3flasher mx35to2_mmc.bin dump 0x00041000 0x00340000 kernel.img
./k3flasher mx35to2_mmc.bin dump 0x00381000 0x00040000 isis.img
./k3flasher mx35to2_mmc.bin dump 0x003c1000 0x28a07000 rootfs.img
./k3flasher mx35to2_mmc.bin dump 0x28dc8000 0x01800000 varfs.img
./k3flasher mx35to2_mmc.bin dump 0x2a5c8000 0x00800000 partition3.img

echo "We do not backup the userstore"
