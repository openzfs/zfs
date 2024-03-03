#!/usr/bin/env bash

######################################################################
# 2) start qemu with some operating system, init via cloud-init
######################################################################

OS="$1"

# valid ostypes: virt-install --os-variant list
OSv=$OS

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
  centos-stream8)
    OSNAME="CentOS Stream 8"
    URL="https://cloud.centos.org/centos/8-stream/x86_64/images/CentOS-Stream-GenericCloud-8-latest.x86_64.qcow2"
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
  fedora38)
    OSNAME="Fedora 38"
    URL="https://download.fedoraproject.org/pub/fedora/linux/releases/38/Cloud/x86_64/images/Fedora-Cloud-Base-38-1.6.x86_64.qcow2"
    ;;
  fedora39)
    OSNAME="Fedora 39"
    OSv="fedora38"
    URL="https://download.fedoraproject.org/pub/fedora/linux/releases/39/Cloud/x86_64/images/Fedora-Cloud-Base-39-1.5.x86_64.qcow2"
    ;;
  freebsd13)
    OSNAME="FreeBSD 13"
    OSv="freebsd13.0"
    # URL="https://download.freebsd.org/ftp/snapshots/amd64"
    # freebsd images don't have clout-init within it! :(
    # -> workaround: provide own images
    URL_ZS="https://openzfs.de/freebsd/amd64-freebsd-13.3.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd14)
    OSNAME="FreeBSD 14"
    OSv="freebsd13.0"
    URL_ZS="https://openzfs.de/freebsd/amd64-freebsd-14.0.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  freebsd15)
    OSNAME="FreeBSD 15"
    OSv="freebsd13.0"
    URL_ZS="https://openzfs.de/freebsd/amd64-freebsd-15.0.qcow2.zst"
    BASH="/usr/local/bin/bash"
    ;;
  ubuntu22)
    OSNAME="Ubuntu 22.04"
    OSv="ubuntu22.04"
    URL="https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img"
    ;;
  ubuntu24)
    OSNAME="Ubuntu 24.04"
    OSv="ubuntu24.04"
    URL="https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img"
    ;;
  *)
    echo "Wrong value for variable OS!"
    exit 111
    ;;
esac

IMG="/mnt/original.qcow2"
DISK="/mnt/openzfs.qcow2"
sudo chown -R $(whoami) /mnt

if [ ! -z "$URL_ZS" ]; then
  echo "Loading image $URL_ZS ..."
  axel -q "$URL_ZS" -o "$IMG.zst" || exit 111
  zstd -d "$IMG.zst" && rm -f "$IMG.zst"
else
  echo "Loading image $URL ..."
  axel -q "$URL" -o "$IMG" || exit 111
fi

# we use zstd for faster IO on the testing runner
echo "Converting image ..."
qemu-img convert -q -f qcow2 -O qcow2 -c -o compression_type=zstd,preallocation=off $IMG $DISK || exit 111
rm -f $IMG || exit 111

echo "Resizing image to 60GiB ..."
qemu-img resize -q $DISK 60G || exit 111

PUBKEY=`cat ~/.ssh/id_ed25519.pub`
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

write_files:
  - path: /tmp/runner-init.sh
    permissions: '0755'
    content: |
      #!/usr/bin/env bash

      cd \$HOME
      exec 2>stderr-init.log
      echo "$OSNAME" > /var/tmp/osname.txt

runcmd:
  - sudo -u zfs /tmp/runner-init.sh
EOF


#   --console pty,target_type=virtio \

# Our instance has 16GB RAM which is way more than we need.  Make three 1GB
# ramdisks for ZTS to use.
# if [ "$OS" == "freebsd*" ] ; then
#    sudo mdconfig -s 1g
#
#    # Ramdisks are /dev/md{0,1,2}
#    DEV="md"
#else
#    # Note: rd_size is in units of KB.
#    sudo modprobe brd rd_nr=3 rd_size=$((1024 * 1024))
#
#    # Ramdisks are /dev/ram{0,1,2}
#    DEV="ram"
#fi

# we could extend this with more virtual disks for example /TR
sudo virt-install \
  --os-variant $OSv \
  --name "openzfs" \
  --cpu host-passthrough \
  --virt-type=kvm \
  --hvm \
  --vcpus=4,sockets=1 \
  --memory $((1024*8)) \
  --graphics none \
  --network bridge=virbr0,model=virtio \
  --cloud-init user-data=/tmp/user-data \
  --disk $DISK,format=qcow2,bus=virtio \
  --import --noautoconsole -v
#  --disk /dev/${DEV}0,size=1,bus=virtio \
#  --disk /dev/${DEV}1,size=1,bus=virtio \
#  --disk /dev/${DEV}2,size=1,bus=virtio \

sudo rm -f /tmp/user-data
