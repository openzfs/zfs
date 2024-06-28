#!/usr/bin/env bash

######################################################################
# 2) start qemu with some operating system, init via cloud-init
######################################################################

set -eu

# valid ostypes: virt-install --os-variant list
OS="$1"
OSv=$OS

# compressed with .zst extension
FREEBSD="https://github.com/mcmilk/openzfs-freebsd-images/releases/download/2024-06-23"
URLzs=""

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
  freebsd13)
    OSNAME="FreeBSD 13"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.3-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14)
    OSNAME="FreeBSD 14"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.1-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd15)
    OSNAME="FreeBSD 15"
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
  ubuntu22)
    OSNAME="Ubuntu 22.04"
    OSv="ubuntu22.04"
    MIRROR="https://cloud-images.ubuntu.com"
    MIRROR="https://mirrors.cloud.tencent.com/ubuntu-cloud-images"
    MIRROR="https://mirror.citrahost.com/ubuntu-cloud-images"
    URL="$MIRROR/jammy/current/jammy-server-cloudimg-amd64.img"
    ;;
  ubuntu24)
    OSNAME="Ubuntu 24.04"
    OSv="ubuntu24.04"
    #URL="https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img"
    #URL="https://mirrors.cloud.tencent.com/ubuntu-cloud-images/noble/current/noble-server-cloudimg-amd64.img"
    URL="https://mirror.citrahost.com/ubuntu-cloud-images/noble/current/noble-server-cloudimg-amd64.img"
    ;;
  *)
    echo "Wrong value for variable OS!"
    exit 111
    ;;
esac

IMG="/mnt/cloudimg.qcow2"
DISK="/mnt/openzfs.qcow2"
sudo chown -R $(whoami) /mnt

if [ ! -z "$URLzs" ]; then
  echo "Loading image $URLzs ..."
  time axel -q -o "$IMG.zst" "$URLzs" || exit 111
  zstd -q -d --rm "$IMG.zst"
else
  echo "Loading image $URL ..."
  time axel -q -o "$IMG" "$URL" || exit 111
fi

# for later use
echo "$OS" > /var/tmp/os.txt
echo "$OSv" > /var/tmp/osvariant.txt
echo "$OSNAME" > /var/tmp/osname.txt

# we use zstd for faster IO on the testing runner
echo "Converting image ..."
qemu-img convert -q -f qcow2 -O qcow2 -c \
  -o compression_type=zstd,preallocation=off $IMG $DISK || exit 111
rm -f $IMG || exit 111

echo "Resizing image to 50GiB ..."
qemu-img resize -q $DISK 50G || exit 111

PUBKEY=`cat ~/.ssh/id_ed25519.pub`
cat <<EOF > /tmp/user-data
#cloud-config

fqdn: $OS

# user:zfs password:1
users:
- name: root
  shell: $BASH
- name: zfs
  sudo: ALL=(ALL) NOPASSWD:ALL
  shell: $BASH
  lock-passwd: false
  passwd: \$1\$EjKAQetN\$O7Tw/rZOHaeBP1AiCliUg/
  ssh_authorized_keys:
    - $PUBKEY

growpart:
  mode: auto
  devices: ['/']
  ignore_growroot_disabled: false
EOF

for i in `seq 0 3`; do
  sudo virsh net-update default add ip-dhcp-host \
    "<host mac='52:54:00:83:79:0$i' ip='192.168.122.1$i'/>" --live --config
done

sudo virt-install \
  --os-variant $OSv \
  --name "openzfs" \
  --cpu host-passthrough \
  --virt-type=kvm --hvm \
  --vcpus=4,sockets=1 \
  --memory $((1024*4)) \
  --memballoon model=none \
  --graphics none \
  --network bridge=virbr0,model=e1000,mac='52:54:00:83:79:00' \
  --cloud-init user-data=/tmp/user-data \
  --controller=scsi,model=virtio-scsi \
  --disk "$DISK",bus=scsi,format=qcow2,driver.discard=unmap \
  --import --noautoconsole
