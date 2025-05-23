#!/usr/bin/env bash

######################################################################
# 1) setup qemu instance on action runner
######################################################################

set -eu

# install needed packages
export DEBIAN_FRONTEND="noninteractive"
sudo apt-get -y update
sudo apt-get install -y axel cloud-image-utils daemonize guestfs-tools \
  virt-manager linux-modules-extra-$(uname -r) zfsutils-linux

# generate ssh keys
rm -f ~/.ssh/id_ed25519
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -q -N ""

# not needed
sudo systemctl stop docker.socket
sudo systemctl stop multipathd.socket

# remove default swapfile and /mnt
sudo swapoff -a
sudo umount -l /mnt
DISK="/dev/disk/cloud/azure_resource-part1"
sudo sed -e "s|^$DISK.*||g" -i /etc/fstab
sudo wipefs -aq $DISK
sudo systemctl daemon-reload

sudo modprobe loop
sudo modprobe zfs

# partition the disk as needed
DISK="/dev/disk/cloud/azure_resource"
sudo sgdisk --zap-all $DISK
sudo sgdisk -p \
 -n 1:0:+16G -c 1:"swap" \
 -n 2:0:0    -c 2:"tests" \
$DISK
sync
sleep 1

# swap with same size as RAM (16GiB)
sudo mkswap $DISK-part1
sudo swapon $DISK-part1

# JBOD 2xdisk for OpenZFS storage (test vm's)
SSD1="$DISK-part2"
sudo fallocate -l 12G /test.ssd2
SSD2=$(sudo losetup -b 4096 -f /test.ssd2 --show)

# adjust zfs module parameter and create pool
exec 1>/dev/null
ARC_MIN=$((1024*1024*256))
ARC_MAX=$((1024*1024*512))
echo $ARC_MIN | sudo tee /sys/module/zfs/parameters/zfs_arc_min
echo $ARC_MAX | sudo tee /sys/module/zfs/parameters/zfs_arc_max
echo 1 | sudo tee /sys/module/zfs/parameters/zvol_use_blk_mq
sudo zpool create -f -o ashift=12 zpool $SSD1 $SSD2 -O relatime=off \
  -O atime=off -O xattr=sa -O compression=lz4 -O sync=disabled \
  -O redundant_metadata=none -O mountpoint=/mnt/tests

# no need for some scheduler
for i in /sys/block/s*/queue/scheduler; do
  echo "none" | sudo tee $i
done
