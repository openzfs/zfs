#!/usr/bin/env bash

######################################################################
# 2) start qemu with some operating system, init via cloud-init
######################################################################

set -eu

# short name used in zfs-qemu.yml
OS="$1"

# OS variant (virt-install --os-variant list)
OSv=$OS

# compressed with .zst extension
REPO="https://github.com/mcmilk/openzfs-freebsd-images"
FREEBSD="$REPO/releases/download/v2024-10-05"
URLzs=""

# Ubuntu mirrors
#UBMIRROR="https://cloud-images.ubuntu.com"
#UBMIRROR="https://mirrors.cloud.tencent.com/ubuntu-cloud-images"
UBMIRROR="https://mirror.citrahost.com/ubuntu-cloud-images"

# default nic model for vm's
NIC="virtio"

case "$OS" in
  almalinux8)
    OSNAME="AlmaLinux 8"
    URL="https://repo.almalinux.org/almalinux/8/cloud/x86_64/images/AlmaLinux-8-GenericCloud-latest.x86_64.qcow2"
    ;;
  almalinux9)
    OSNAME="AlmaLinux 9"
    URL="https://repo.almalinux.org/almalinux/9/cloud/x86_64/images/AlmaLinux-9-GenericCloud-latest.x86_64.qcow2"
    ;;
  archlinux)
    OSNAME="Archlinux"
    URL="https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2"
    # dns sometimes fails with that url  :/
    echo "89.187.191.12  geo.mirror.pkgbuild.com" | sudo tee /etc/hosts > /dev/null
    ;;
  centos-stream9)
    OSNAME="CentOS Stream 9"
    URL="https://cloud.centos.org/centos/9-stream/x86_64/images/CentOS-Stream-GenericCloud-9-latest.x86_64.qcow2"
    ;;
  debian11)
    OSNAME="Debian 11"
    URL="https://cloud.debian.org/images/cloud/bullseye/latest/debian-11-generic-amd64.qcow2"
    ;;
  debian12)
    OSNAME="Debian 12"
    URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-generic-amd64.qcow2"
    ;;
  fedora39)
    OSNAME="Fedora 39"
    OSv="fedora39"
    URL="https://download.fedoraproject.org/pub/fedora/linux/releases/39/Cloud/x86_64/images/Fedora-Cloud-Base-39-1.5.x86_64.qcow2"
    ;;
  fedora40)
    OSNAME="Fedora 40"
    OSv="fedora39"
    URL="https://download.fedoraproject.org/pub/fedora/linux/releases/40/Cloud/x86_64/images/Fedora-Cloud-Base-Generic.x86_64-40-1.14.qcow2"
    ;;
  freebsd13-3r)
    OSNAME="FreeBSD 13.3-RELEASE"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.3-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    NIC="rtl8139"
    ;;
  freebsd13-4r)
    OSNAME="FreeBSD 13.4-RELEASE"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.4-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    NIC="rtl8139"
    ;;
  freebsd14-0r)
    OSNAME="FreeBSD 14.0-RELEASE"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.0-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14-1r)
    OSNAME="FreeBSD 14.1-RELEASE"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.1-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd13-4s)
    OSNAME="FreeBSD 13.4-STABLE"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.4-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14-1s)
    OSNAME="FreeBSD 14.1-STABLE"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.1-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd15-0c)
    OSNAME="FreeBSD 15.0-CURRENT"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-15.0-CURRENT.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  tumbleweed)
    OSNAME="openSUSE Tumbleweed"
    OSv="opensusetumbleweed"
    MIRROR="http://opensuse-mirror-gce-us.susecloud.net"
    URL="$MIRROR/tumbleweed/appliances/openSUSE-MicroOS.x86_64-OpenStack-Cloud.qcow2"
    ;;
  ubuntu20)
    OSNAME="Ubuntu 20.04"
    OSv="ubuntu20.04"
    URL="$UBMIRROR/focal/current/focal-server-cloudimg-amd64.img"
    ;;
  ubuntu22)
    OSNAME="Ubuntu 22.04"
    OSv="ubuntu22.04"
    URL="$UBMIRROR/jammy/current/jammy-server-cloudimg-amd64.img"
    ;;
  ubuntu24)
    OSNAME="Ubuntu 24.04"
    OSv="ubuntu24.04"
    URL="$UBMIRROR/noble/current/noble-server-cloudimg-amd64.img"
    ;;
  *)
    echo "Wrong value for OS variable!"
    exit 111
    ;;
esac

# environment file
ENV="/var/tmp/env.txt"
echo "ENV=$ENV" >> $ENV

# result path
echo 'RESPATH="/var/tmp/test_results"' >> $ENV

# FreeBSD 13 has problems with: e1000+virtio
echo "NIC=$NIC" >> $ENV

# freebsd15 -> used in zfs-qemu.yml
echo "OS=$OS" >> $ENV

# freebsd14.0 -> used for virt-install
echo "OSv=\"$OSv\"" >> $ENV

# FreeBSD 15 (Current) -> used for summary
echo "OSNAME=\"$OSNAME\"" >> $ENV

sudo mkdir -p "/mnt/tests"
sudo chown -R $(whoami) /mnt/tests

# we are downloading via axel, curl and wget are mostly slower and
# require more return value checking
IMG="/mnt/tests/cloudimg.qcow2"
if [ ! -z "$URLzs" ]; then
  echo "Loading image $URLzs ..."
  time axel -q -o "$IMG.zst" "$URLzs"
  zstd -q -d --rm "$IMG.zst"
else
  echo "Loading image $URL ..."
  time axel -q -o "$IMG" "$URL"
fi

DISK="/dev/zvol/zpool/openzfs"
FORMAT="raw"
sudo zfs create -ps -b 64k -V 80g zpool/openzfs
while true; do test -b $DISK && break; sleep 1; done
echo "Importing VM image to zvol..."
sudo qemu-img dd -f qcow2 -O raw if=$IMG of=$DISK bs=4M
rm -f $IMG

PUBKEY=$(cat ~/.ssh/id_ed25519.pub)
cat <<EOF > /tmp/user-data
#cloud-config

fqdn: $OS

users:
- name: root
  shell: $BASH
- name: zfs
  sudo: ALL=(ALL) NOPASSWD:ALL
  shell: $BASH
  ssh_authorized_keys:
    - $PUBKEY

growpart:
  mode: auto
  devices: ['/']
  ignore_growroot_disabled: false
EOF

sudo virsh net-update default add ip-dhcp-host \
  "<host mac='52:54:00:83:79:00' ip='192.168.122.10'/>" --live --config

sudo virt-install \
  --os-variant $OSv \
  --name "openzfs" \
  --cpu host-passthrough \
  --virt-type=kvm --hvm \
  --vcpus=4,sockets=1 \
  --memory $((1024*12)) \
  --memballoon model=virtio \
  --graphics none \
  --network bridge=virbr0,model=$NIC,mac='52:54:00:83:79:00' \
  --cloud-init user-data=/tmp/user-data \
  --disk $DISK,bus=virtio,cache=none,format=$FORMAT,driver.discard=unmap \
  --import --noautoconsole >/dev/null

# in case the directory isn't there already
mkdir -p $HOME/.ssh

cat <<EOF >> $HOME/.ssh/config
# no questions please
StrictHostKeyChecking no

# small timeout, used in while loops later
ConnectTimeout 1
EOF
