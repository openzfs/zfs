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
  sudo pacman -Sy --noconfirm base-devel bc cpio dkms fakeroot fio \
    inetutils  linux linux-headers lsscsi nfs-utils parted pax perf \
    python-packaging python-setuptools ksh samba sysstat rng-tools \
    rsync wget
  echo "##[endgroup]"
}

function debian() {
  export DEBIAN_FRONTEND=noninteractive

  echo "##[group]Running apt-get update+upgrade"
  sudo apt-get update -y
  sudo apt-get upgrade -y
  echo "##[endgroup]"

  echo "##[group]Install Development Tools"
  yes | sudo apt-get install -y build-essential autoconf libtool \
    libtool-bin gdb lcov git alien fakeroot wget curl bc fio acl \
    sysstat lsscsi parted gdebi attr dbench watchdog ksh nfs-kernel-server \
    samba rng-tools dkms rsync linux-headers-$(uname -r) \
    zlib1g-dev uuid-dev libblkid-dev libselinux-dev \
    xfslibs-dev libattr1-dev libacl1-dev libudev-dev libdevmapper-dev \
    libssl-dev libaio-dev libffi-dev libelf-dev libmount-dev \
    libpam0g-dev pamtester python3-dev python3-setuptools python3 \
    python3-dev python3-setuptools python3-cffi libcurl4-openssl-dev \
    python3-packaging python3-distlib dh-python python3-all-dev python3-sphinx

  # dkms related
  yes | sudo apt-get install -y build-essential:native dh-sequence-dkms \
    libpam0g-dev rpm2cpio cpio || true
  echo "##[endgroup]"
}

function freebsd() {
  export ASSUME_ALWAYS_YES="YES"

  echo "##[group]Install Development Tools"
  sudo pkg install -y autoconf automake autotools base64 fio gdb git gmake \
    gsed python python3 gettext gettext-runtime checkbashisms lcov libtool \
    lscpu ksh93 pamtester pamtester rsync

  sudo pkg install -xy \
    '^samba4[[:digit:]]+$' \
    '^py3[[:digit:]]+-cffi$' \
    '^py3[[:digit:]]+-sysctl$' \
    '^py3[[:digit:]]+-packaging$'

  echo "##[endgroup]"

  # the image has /usr/src already
  #echo "##[group]Install Kernel Headers"
  #VERSION=$(freebsd-version -r)
  #sudo mkdir -p /usr/src
  #sudo chown -R $(whoami) /usr/src
  #git clone --depth=1 -b releng/${VERSION%%-*} https://github.com/freebsd/freebsd-src /usr/src ||
  #git clone --depth=1 -b stable/${VERSION%%.*} https://github.com/freebsd/freebsd-src /usr/src ||
  #git clone --depth=1 -b main https://github.com/freebsd/freebsd-src /usr/src
  #echo "##[endgroup]"
}

function rhel() {
  echo "##[group]Running dnf update"
  sudo dnf update -y
  echo "##[endgroup]"

  echo "##[group]Install Development Tools"
  sudo dnf group install -y "Development Tools"
  sudo dnf install -y libtool rpm-build libtirpc-devel libblkid-devel \
    libuuid-devel libudev-devel openssl-devel zlib-devel libaio-devel \
    libattr-devel elfutils-libelf-devel python3 python3-devel \
    python3-setuptools python3-cffi libffi-devel git ncompress \
    libcurl-devel python3-packaging systemd
  sudo dnf install -y kernel-devel-$(uname -r)


  # Required development tools.
  sudo -E yum -y --skip-broken install gcc make autoconf libtool gdb \
    kernel-rpm-macros kernel-abi-whitelists

  echo "##[endgroup]"

  echo "##[group]Install utilities for ZTS"
  sudo dnf install -y acl ksh bc bzip2 fio sysstat mdadm lsscsi parted attr \
    nfs-utils samba rng-tools perf rsync dbench pamtester
  echo "##[endgroup]"
}

# Enable and startup nfs and smb
function enable_services() {
  echo "##[group]starting services"

    case "$1" in
    ubuntu*|debian11*)
        sudo -E systemctl enable nfs-kernel-server
        sudo -E systemctl enable smbd
        ;;
    debian*)
        sudo -E systemctl enable nfs-kernel-server
        sudo -E systemctl enable samba
        ;;
    freebsd*)
        sudo -E touch /etc/zfs/exports
        sudo -E sysrc mountd_flags="/etc/zfs/exports"
        sudo -E service nfsd enable
        echo '[global]' | sudo -E tee /usr/local/etc/smb4.conf >/dev/null
        sudo -E service samba_server enable
        ;;

    # Fedora, Alma, most other distros
    *)
        sudo -E systemctl enable nfs-server
        sudo -E systemctl enable smb
        ;;
  esac

  echo "##[endgroup]"
}

case "$1" in
  almalinux8|centos-stream8)
    echo "##[group]Enable epel and powertools repositories"
    sudo dnf config-manager -y --set-enabled powertools
    sudo dnf install -y epel-release
    echo "##[endgroup]"
    rhel
    ;;
  almalinux9|centos-stream9)
    echo "##[group]Enable epel and crb repositories"
    sudo dnf config-manager -y --set-enabled crb
    sudo dnf install -y epel-release

   sudo dnf -y install kernel-abi-stablelists

    # To minimize EPEL leakage, disable by default...
    sudo -E sed -e "s/enabled=1/enabled=0/g" -i /etc/yum.repos.d/epel.repo

     # Required utilities.
    sudo -E yum -y --skip-broken install --enablerepo=epel git rpm-build \
        wget curl bc fio acl sysstat mdadm lsscsi parted attr dbench watchdog \
        ksh nfs-utils samba rng-tools dkms pamtester ncompress rsync

    # Required development libraries
    sudo -E yum -y --skip-broken install kernel-devel \
        zlib-devel libuuid-devel libblkid-devel libselinux-devel \
        xfsprogs-devel libattr-devel libacl-devel libudev-devel \
        openssl-devel libargon2-devel libffi-devel pam-devel libaio-devel libcurl-devel

    sudo -E yum -y --skip-broken install --enablerepo=powertools \
        python3-packaging rpcgen

    echo "##[endgroup]"
    rhel
    ;;
  archlinux)
    archlinux
    ;;
  debian*)
    debian
    echo "##[group]Install linux-perf"
    sudo apt-get install -yq linux-perf
    echo "##[endgroup]"
    ;;
  fedora*)
    rhel

    ;;
  freebsd*)
    freebsd
    ;;
  ubuntu*)
    debian
    echo "##[group]Install linux-tools-common"
    sudo apt-get install -yq linux-tools-common libtirpc-dev
    echo "##[endgroup]"
    ;;
esac

enable_services "$1"

# Enable serial console and remove 'quiet' from cmdline so we see all kernel
# messages.
#
# This is most certainly overkill, but designed to work on all distos
if [ "$1" != "freebsd*" ] ; then
    sudo sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="console=ttyS0,115200n8 /g; s/quiet //g' /etc/default/grub || true
fi

for i in /boot/grub/grub.cfg /etc/grub2.cfg /etc/grub2-efi.cfg /boot/grub2/grub.cfg ; do
    if [ -e $i ] ; then
        sudo grub-mkconfig -o $i
    fi
done

sudo poweroff
