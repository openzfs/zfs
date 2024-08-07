#!/usr/bin/env bash

######################################################################
# 3) install dependencies for compiling and loading
######################################################################

set -eu

function archlinux() {
  echo "##[group]Running pacman -Syu"
  sudo pacman -Syu --noconfirm
  echo "##[endgroup]"

  echo "##[group]Install Development Tools"
  sudo pacman -Sy --noconfirm base-devel bc cpio dhclient dkms fakeroot \
    fio gdb inetutils less linux linux-headers lsscsi nfs-utils parted pax \
    perf python-packaging python-setuptools ksh samba sysstat rng-tools \
    rsync wget
  echo "##[endgroup]"
}

function debian() {
  export DEBIAN_FRONTEND="noninteractive"

  echo "##[group]Running apt-get update+upgrade"
  sudo apt-get update -y
  sudo apt-get upgrade -y
  echo "##[endgroup]"

  echo "##[group]Install Development Tools"
  sudo apt-get install -y \
    acl alien attr autoconf bc cpio curl dbench dh-python \
    dkms fakeroot fio gdb gdebi git ksh lcov \
    isc-dhcp-client libacl1-dev libaio-dev libattr1-dev libblkid-dev \
    libcurl4-openssl-dev libdevmapper-dev libelf-dev libffi-dev \
    libmount-dev libpam0g-dev libselinux-dev libssl-dev libtool \
    libtool-bin libudev-dev linux-headers-$(uname -r) lsscsi \
    nfs-kernel-server pamtester parted python3 python3-all-dev \
    python3-cffi python3-dev python3-distlib python3-packaging \
    python3-setuptools python3-sphinx rng-tools rpm2cpio rsync samba \
    sysstat uuid-dev watchdog wget xfslibs-dev zlib1g-dev
  echo "##[endgroup]"
}

function freebsd() {
  export ASSUME_ALWAYS_YES="YES"

  echo "##[group]Install Development Tools"
  sudo pkg install -y autoconf automake autotools base64 checkbashisms fio \
    gdb git gmake gsed pkgconf python python3 gettext gettext-runtime \
    lcov libtool lscpu ksh93 pamtester pamtester rsync
  sudo pkg install -xy \
    '^samba4[[:digit:]]+$' \
    '^py3[[:digit:]]+-cffi$' \
    '^py3[[:digit:]]+-sysctl$' \
    '^py3[[:digit:]]+-packaging$'
  echo "##[endgroup]"
}

# common packages for: almalinux, centos, redhat
function rhel() {
  echo "##[group]Running dnf update"
  echo "max_parallel_downloads=10" | sudo -E tee -a /etc/dnf/dnf.conf
  sudo dnf clean all
  sudo dnf update -y --setopt=fastestmirror=1 --refresh
  echo "##[endgroup]"

  echo "##[group]Install Development Tools"
  sudo dnf group install -y "Development Tools"
  sudo dnf install -y \
    acl attr bc bzip2 curl dbench dkms elfutils-libelf-devel fio gdb git \
    kernel-rpm-macros ksh libacl-devel libaio-devel libargon2-devel \
    libattr-devel libblkid-devel libcurl-devel libffi-devel ncompress \
    libselinux-devel libtirpc-devel libtool libudev-devel libuuid-devel \
    lsscsi mdadm nfs-utils openssl-devel pam-devel pamtester parted perf \
    python3 python3-cffi python3-devel python3-packaging kernel-devel \
    python3-setuptools rng-tools rpcgen rpm-build rsync samba sysstat \
    systemd watchdog wget xfsprogs-devel zlib-devel
  echo "##[endgroup]"
}

function tumbleweed() {
  echo "##[group]Running zypper is TODO!"
  sleep 23456
  echo "##[endgroup]"
}

# Install dependencies
case "$1" in
  almalinux8)
    echo "##[group]Enable epel and powertools repositories"
    sudo dnf config-manager -y --set-enabled powertools
    sudo dnf install -y epel-release
    echo "##[endgroup]"
    rhel
    echo "##[group]Install kernel-abi-whitelists"
    sudo dnf install -y kernel-abi-whitelists
    echo "##[endgroup]"
    ;;
  almalinux9|centos-stream9)
    echo "##[group]Enable epel and crb repositories"
    sudo dnf config-manager -y --set-enabled crb
    sudo dnf install -y epel-release
    echo "##[endgroup]"
    rhel
    echo "##[group]Install kernel-abi-stablelists"
    sudo dnf install -y kernel-abi-stablelists
    echo "##[endgroup]"
    ;;
  archlinux)
    archlinux
    ;;
  debian*)
    debian
    echo "##[group]Install Debian specific"
    sudo apt-get install -yq linux-perf dh-sequence-dkms
    echo "##[endgroup]"
    ;;
  fedora*)
    rhel
    ;;
  freebsd*)
    freebsd
    ;;
  tumbleweed)
    tumbleweed
    ;;
  ubuntu*)
    debian
    echo "##[group]Install Ubuntu specific"
    sudo apt-get install -yq linux-tools-common libtirpc-dev \
      linux-modules-extra-$(uname -r)
    if [ "$1" != "ubuntu20" ]; then
      sudo apt-get install -yq dh-sequence-dkms
    fi
    echo "##[endgroup]"
    echo "##[group]Delete Ubuntu OpenZFS modules"
    for i in `find /lib/modules -name zfs -type d`; do sudo rm -rvf $i; done
    echo "##[endgroup]"
    ;;
esac

# Start services
echo "##[group]Enable services"
case "$1" in
  freebsd*)
    # add virtio things
    echo 'virtio_load="YES"' | sudo -E tee -a /boot/loader.conf
    #for i in balloon blk pci scsi console; do
    #  echo "virtio_${i}_load=\"YES\"" | sudo -E tee -a /boot/loader.conf
    #done
    echo "fdescfs /dev/fd fdescfs rw 0 0" | sudo -E tee -a /etc/fstab
    sudo -E mount /dev/fd
    sudo -E touch /etc/zfs/exports
    sudo -E sysrc mountd_flags="/etc/zfs/exports"
    sudo -E service nfsd enable
    echo '[global]' | sudo -E tee /usr/local/etc/smb4.conf >/dev/null
    sudo -E service samba_server enable
    ;;
  debian*|ubuntu*)
    sudo -E systemctl enable nfs-kernel-server
    sudo -E systemctl enable smbd
    ;;
  *)
    # All other linux distros
    sudo -E systemctl enable nfs-server
    sudo -E systemctl enable smb
    ;;
esac
echo "##[endgroup]"

# Enable serial console and remove 'quiet' from linux kernel cmdline
case "$1" in
  freebsd*)
    true
    ;;
  *)
    echo "##[group]Enable serial output"
    sudo sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="console=ttyS0,115200n8 random.trust_cpu=on/g; s/quiet //g' /etc/default/grub || true
    for i in /boot/grub/grub.cfg /etc/grub2.cfg /etc/grub2-efi.cfg /boot/grub2/grub.cfg ; do
      test -e $i || continue
      echo sudo grub-mkconfig -o $i
      sudo grub-mkconfig -o $i
    done
    echo "##[endgroup]"
    ;;
esac

# ssh config
mkdir -p $HOME/.ssh
echo "StrictHostKeyChecking no" >> $HOME/.ssh/config
echo "ConnectTimeout 1" >> $HOME/.ssh/config

sleep 2 && sudo poweroff &
exit 0
