#!/usr/bin/env bash

######################################################################
# 1) setup the action runner to start some qemu instance
######################################################################

set -eu

# docker isn't needed, free some memory
sudo systemctl stop docker.socket
sudo apt-get remove docker-ce-cli docker-ce podman

# remove snapd, not needed
for x in lxd core20 snapd; do sudo snap remove $x; done
sudo apt-get remove google-chrome-stable firefox snapd

# only install qemu
sudo apt-get update
sudo apt-get install axel cloud-image-utils guestfs-tools virt-manager

# no swap needed
sudo swapoff -a

# disk usage afterwards
sudo df -h /
sudo df -h /mnt
sudo fstrim -a

# generate ssh keys
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -q -N ""
