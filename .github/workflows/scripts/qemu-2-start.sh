#!/usr/bin/env bash

######################################################################
# 2) start qemu with some operating system, init via cloud-init
######################################################################

set -eu

# valid ostypes: virt-install --os-variant list
OS="$1"
VMs="$2"
OSv=$OS

# compressed with .zst extension
FREEBSD="https://github.com/mcmilk/openzfs-freebsd-images/releases/download/v2024-07-24"
URLzs=""

# Ubuntu mirrors
#UBMIRROR="https://cloud-images.ubuntu.com"
#UBMIRROR="https://mirrors.cloud.tencent.com/ubuntu-cloud-images"
UBMIRROR="https://mirror.citrahost.com/ubuntu-cloud-images"

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
    OSNAME="FreeBSD 13 (Stable)"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.3-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd13r)
    OSNAME="FreeBSD 13 (Release)"
    OSv="freebsd13.0"
    URLzs="$FREEBSD/amd64-freebsd-13.3-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14)
    OSNAME="FreeBSD 14 (Stable)"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.1-STABLE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14r)
    OSNAME="FreeBSD 14 (Release)"
    OSv="freebsd14.0"
    URLzs="$FREEBSD/amd64-freebsd-14.1-RELEASE.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd15)
    OSNAME="FreeBSD 15 (Current)"
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
    echo "Wrong value for variable OS!"
    exit 111
    ;;
esac

# freebsd15 -> used in zfs-qemu.yml
echo "$OS" > /var/tmp/os.txt

# freebsd14.0 -> used for virt-install
echo "$OSv" > /var/tmp/osvariant.txt

# FreeBSD 15 (Current) -> used for summary
echo "$OSNAME" > /var/tmp/osname.txt

# 3 -> number of virtual machines used for testings
echo "$VMs" > /var/tmp/vms.txt

IMG="/mnt/tests/cloudimg.qcow2"
DISK="/mnt/tests/openzfs.qcow2"
sudo chown -R $(whoami) /mnt

if [ ! -z "$URLzs" ]; then
  echo "Loading image $URLzs ..."
  time axel -q -o "$IMG.zst" "$URLzs"
  zstd -q -d --rm "$IMG.zst"
else
  echo "Loading image $URL ..."
  time axel -q -o "$IMG" "$URL"
fi

qemu-img convert -q -f qcow2 -O qcow2 -c \
  -o compression_type=zstd,cluster_size=128k $IMG $DISK
rm -f $IMG

echo "Resizing image to 40GiB ..."
qemu-img resize -q $DISK 40G

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

for i in `seq 0 $VMs`; do
  sudo virsh net-update default add ip-dhcp-host \
    "<host mac='52:54:00:83:79:0$i' ip='192.168.122.1$i'/>" --live --config
done

sudo virt-install \
  --os-variant $OSv \
  --name "openzfs" \
  --cpu host-passthrough \
  --virt-type=kvm --hvm \
  --vcpus=4,sockets=1 \
  --memory $((1024*8)) \
  --memballoon model=virtio,autodeflate=on,freePageReporting=on \
  --graphics none \
  --network bridge=virbr0,model=e1000,mac='52:54:00:83:79:00' \
  --cloud-init user-data=/tmp/user-data \
  --disk $DISK,bus=virtio,cache=none,format=qcow2,driver.discard=unmap \
  --import --noautoconsole >/dev/null
