#!/usr/bin/env bash

######################################################################
# 1) setup qemu instance on action runner
######################################################################

set -eu

# The default 'azure.archive.ubuntu.com' mirrors can be really slow.
# Prioritize the official Ubuntu mirrors.
#
# The normal apt-mirrors.txt will look like:
#
# http://azure.archive.ubuntu.com/ubuntu/       priority:1
# https://archive.ubuntu.com/ubuntu/    priority:2
# https://security.ubuntu.com/ubuntu/   priority:3
#
# Just delete the 'azure.archive.ubuntu.com' line.
sudo sed -i '/azure.archive.ubuntu.com/d' /etc/apt/apt-mirrors.txt
echo "Using mirrors:"
cat /etc/apt/apt-mirrors.txt

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

sudo swapoff -a

# Special case:
#
# For reasons unknown, the runner can boot-up with two different block device
# configurations.  On one config you get two 75GB block devices, and on the
# other you get a single 150GB block device. Here's what both look like:
#
# --- Two 75GB block devices ---
# NAME    MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
# sda       8:0    0  150G  0 disk
# ├─sda1    8:1    0  149G  0 part /
# ├─sda14   8:14   0    4M  0 part
# ├─sda15   8:15   0  106M  0 part /boot/efi
# └─sda16 259:0    0  913M  0 part /boot
#
# lrwxrwxrwx 1 root root  9 Jan 29 18:07 azure_root -> ../../sda
# lrwxrwxrwx 1 root root 10 Jan 29 18:07 azure_root-part1 -> ../../sda1
# lrwxrwxrwx 1 root root 11 Jan 29 18:07 azure_root-part14 -> ../../sda14
# lrwxrwxrwx 1 root root 11 Jan 29 18:07 azure_root-part15 -> ../../sda15
# lrwxrwxrwx 1 root root 11 Jan 29 18:07 azure_root-part16 -> ../../sda16
#
# --- One 150GB block device ---
# NAME    MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
# sda       8:0    0   75G  0 disk
# ├─sda1    8:1    0   74G  0 part /
# ├─sda14   8:14   0    4M  0 part
# ├─sda15   8:15   0  106M  0 part /boot/efi
# └─sda16 259:0    0  913M  0 part /boot
# sdb       8:16   0   75G  0 disk
# └─sdb1    8:17   0   75G  0 part
#
# lrwxrwxrwx 1 root root  9 Jan 29 18:07 azure_resource -> ../../sdb
# lrwxrwxrwx 1 root root 10 Jan 29 18:07 azure_resource-part1 -> ../../sdb1
# lrwxrwxrwx 1 root root  9 Jan 29 18:07 azure_root -> ../../sda
# lrwxrwxrwx 1 root root 10 Jan 29 18:07 azure_root-part1 -> ../../sda1
# lrwxrwxrwx 1 root root 11 Jan 29 18:07 azure_root-part14 -> ../../sda14
# lrwxrwxrwx 1 root root 11 Jan 29 18:07 azure_root-part15 -> ../../sda15
#
# If we have the azure_resource-part1 partition, umount it, partition it, and
# use it as our ZFS disk and swap partition.  If not, just create a file VDEV
# and swap file and use that instead.

# remove default swapfile and /mnt
if [ -e /dev/disk/cloud/azure_resource-part1 ] ; then
  sudo umount -l /mnt
  DISK="/dev/disk/cloud/azure_resource-part1"
  sudo sed -e "s|^$DISK.*||g" -i /etc/fstab
  sudo wipefs -aq $DISK
  sudo systemctl daemon-reload
fi

sudo modprobe loop
sudo modprobe zfs

if [ -e /dev/disk/cloud/azure_resource-part1 ] ; then
  echo "We have two 75GB block devices"
  # partition the disk as needed
  DISK="/dev/disk/cloud/azure_resource"
  sudo sgdisk --zap-all $DISK
  sudo sgdisk -p \
   -n 1:0:+16G -c 1:"swap" \
   -n 2:0:0    -c 2:"tests" \
   $DISK
  sync
  sleep 1

  sudo fallocate -l 12G /test.ssd2
  DISKS="$DISK-part2 /test.ssd2"

  SWAP=$DISK-part1
else
  echo "We have a single 150GB block device"
  sudo fallocate -l 72G /test.ssd2
  SWAP=/swapfile.ssd
  sudo fallocate -l 16G $SWAP
  sudo chmod 600 $SWAP
  DISKS="/test.ssd2"
fi

# swap with same size as RAM (16GiB)
sudo mkswap $SWAP
sudo swapon $SWAP

# adjust zfs module parameter and create pool
exec 1>/dev/null
ARC_MIN=$((1024*1024*256))
ARC_MAX=$((1024*1024*512))
echo $ARC_MIN | sudo tee /sys/module/zfs/parameters/zfs_arc_min
echo $ARC_MAX | sudo tee /sys/module/zfs/parameters/zfs_arc_max
echo 1 | sudo tee /sys/module/zfs/parameters/zvol_use_blk_mq
sudo zpool create -f -o ashift=12 zpool $DISKS -O relatime=off \
  -O atime=off -O xattr=sa -O compression=lz4 -O sync=disabled \
  -O redundant_metadata=none -O mountpoint=/mnt/tests

# no need for some scheduler
for i in /sys/block/s*/queue/scheduler; do
  echo "none" | sudo tee $i
done
