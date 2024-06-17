#!/usr/bin/env bash

######################################################################
# 1) setup the action runner to start some qemu instance
######################################################################

set -eu

# Filesystem      Size  Used Avail Use% Mounted on
# /dev/root        72G   42G   31G  58% /     -> 25GB
# /dev/sdb1        74G  4.1G   66G   6% /mnt  -> 60GB

# speedup virtual qemu disk a bit
sudo modprobe loop max_loop=2
sudo fallocate -l 25GB /var/blob
sudo fallocate -l 60GB /mnt/blob
sudo losetup -f /var/blob
sudo losetup -f /mnt/blob
sudo mdadm --create /dev/md/raidzero -n2 -l0 /dev/loop{0..1}
sudo mkfs.ext4 -F /dev/md/raidzero
sudo mkdir -p /mnt/tests
sudo mount -o discard,noatime /dev/md/raidzero /mnt/tests

# install needed packages
sudo apt-get update
sudo apt-get install axel cloud-image-utils daemonize guestfs-tools \
  ksmtuned virt-manager linux-modules-extra-`uname -r`
sudo systemctl start ksm
sudo systemctl start ksmtuned

# generate ssh keys
rm -f ~/.ssh/id_ed25519
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -q -N ""

# no need for some scheduler
for i in /sys/block/s*/queue/scheduler; do
  echo "none" | sudo tee $i
done
