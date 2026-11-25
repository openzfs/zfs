#!/usr/bin/env bash

######################################################################
# 1) setup qemu instance on action runner
######################################################################

set -eu

# The default runner has a bunch of development tools and other things
# that we do not need.  Remove them here to free up a total of 35GB.
#
# First remove packages - this frees up ~10GB
echo "Disk space before purge:"
df -h /
sudo docker image prune --all --force
sudo docker builder prune -a

probe_mirror() {
  local url=$1 path=$2 spd
  spd=$(curl -4 -fsSL -r 0-524287 --connect-timeout 2 --max-time 3 \
    -o /dev/null -w '%{speed_download}' "${url}${path}" 2>/dev/null || echo 0)
  printf '%s %s\n' "$spd" "$url"
}

setup_fast_apt_mirrors() {
  local codename probe_path tmp m
  sudo tee /etc/apt/apt.conf.d/99-fast-mirrors >/dev/null <<'EOF'
Acquire::Retries "2";
Acquire::http::Timeout "10";
Acquire::https::Timeout "10";
EOF

  codename=$(. /etc/os-release && echo "${VERSION_CODENAME:-noble}")
  probe_path="dists/${codename}/main/binary-amd64/Packages.xz"
  tmp=$(mktemp)

  for m in $(
    {
      curl -4 -fsSL --max-time 5 http://mirrors.ubuntu.com/US.txt 2>/dev/null \
        | shuf -n 12 || true
      printf '%s\n' "https://mirrors.edge.kernel.org/ubuntu/" \
        "https://archive.ubuntu.com/ubuntu/"
    } | awk 'NF && !seen[$0]++'
  ); do
    probe_mirror "$m" "$probe_path" >> "$tmp" &
  done
  wait

  {
    awk '$1 + 0 > 0' "$tmp" | sort -rn | head -n 5 | awk '{print $2}'
    printf '%s\n' "https://archive.ubuntu.com/ubuntu/" \
      "https://security.ubuntu.com/ubuntu/"
  } | awk 'NF && !seen[$0]++ {printf "%s\tpriority:%d\n", $0, ++p}' \
    | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
  rm -f "$tmp"

  echo "Using mirrors:"
  cat /etc/apt/apt-mirrors.txt
}

install_apt_packages() {
  if ! timeout 5m sudo apt-get install -y "$@"; then
    sudo sed -i '1d' /etc/apt/apt-mirrors.txt
    echo "Retrying with next mirror:"
    cat /etc/apt/apt-mirrors.txt
    sudo apt-get -y update
    sudo apt-get install -y "$@"
  fi
}

setup_fast_apt_mirrors

export DEBIAN_FRONTEND="noninteractive"
sudo apt-get -y update

unneeded="microsoft-edge-stable|azure-cli|google-cloud|google-chrome-stable|"\
"temurin|llvm|firefox|mysql-server|snapd|android|dotnet|haskell|ghcup|"\
"powershell|julia|swift|miniconda|chromium"
pkgs=$(dpkg-query -f '${binary:Package}\n' -W | grep -E "$unneeded" || true)
if [ -n "$pkgs" ]; then
  # shellcheck disable=SC2086
  sudo apt-get -y remove $pkgs
fi
sudo apt-get -y autoremove

# Next, remove unneeded files in /usr.  This frees up an additional 25GB.
sudo rm -fr /usr/local/lib/android /usr/share/dotnet /usr/local/.ghcup \
        /usr/share/swift /usr/local/share/powershell /usr/local/julia* \
        /usr/share/miniconda /usr/local/share/chromium
echo "Disk space after:"
df -h /

install_apt_packages axel cloud-image-utils daemonize guestfs-tools \
  virt-manager zfsutils-linux


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
# --- One 150GB block device ---
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
# --- Two 75GB block devices ---
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

echo "Block devices:"
lsblk

# adjust zfs module parameter and create pool
ARC_MIN=$((1024*1024*256))
ARC_MAX=$((1024*1024*512))
echo $ARC_MIN | sudo tee /sys/module/zfs/parameters/zfs_arc_min >/dev/null
echo $ARC_MAX | sudo tee /sys/module/zfs/parameters/zfs_arc_max >/dev/null
echo 1 | sudo tee /sys/module/zfs/parameters/zvol_use_blk_mq >/dev/null
sudo zpool create -f -o ashift=12 zpool $DISKS -O relatime=off \
  -O atime=off -O xattr=sa -O compression=lz4 -O sync=disabled \
  -O redundant_metadata=none -O mountpoint=/mnt/tests
echo "Status:"
zpool status

echo "Last dmesg:"
sudo dmesg | tail -n 10
