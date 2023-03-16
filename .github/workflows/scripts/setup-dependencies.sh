#!/usr/bin/env bash

set -eu

function prerun() {
  echo "::group::Install build dependencies"
  # remove snap things, update+upgrade will be faster then
  for x in lxd core20 snapd; do sudo snap remove $x; done
  sudo apt-get purge snapd google-chrome-stable firefox
  # https://github.com/orgs/community/discussions/47863
  sudo apt-get remove grub-efi-amd64-bin grub-efi-amd64-signed shim-signed --allow-remove-essential
  sudo apt-get update
  sudo apt upgrade
  sudo xargs --arg-file=.github/workflows/build-dependencies.txt apt-get install -qq
  sudo apt-get clean
  sudo dmesg -c > /var/tmp/dmesg-prerun
  echo "::endgroup::"
}

function mod_build() {
  echo "::group::Generate debian packages"
  ./autogen.sh
  ./configure --enable-debug --enable-debuginfo --enable-asan --enable-ubsan
  make --no-print-directory --silent native-deb-utils native-deb-kmod
  mv ../*.deb .
  rm ./openzfs-zfs-dracut*.deb ./openzfs-zfs-dkms*.deb
  echo "$ImageOS-$ImageVersion" > tests/ImageOS.txt
  echo "::endgroup::"
}

function mod_install() {
  # install the pre-built module only on the same runner image
  MOD=`cat tests/ImageOS.txt`
  if [ "$MOD" != "$ImageOS-$ImageVersion" ]; then
    rm -f *.deb
    mod_build
  fi

  echo "::group::Install and load modules"
  # don't use kernel-shipped zfs modules
  sudo sed -i.bak 's/updates/extra updates/' /etc/depmod.d/ubuntu.conf
  sudo apt-get install --fix-missing ./*.deb

  # Native Debian packages enable and start the services
  # Stop zfs-zed daemon, as it may interfere with some ZTS test cases
  sudo systemctl stop zfs-zed
  sudo depmod -a
  sudo modprobe zfs
  sudo dmesg
  sudo dmesg -c > /var/tmp/dmesg-module-load
  echo "::endgroup::"

  echo "::group::Report CPU information"
  lscpu
  cat /proc/spl/kstat/zfs/chksum_bench
  echo "::endgroup::"

  echo "::group::Reclaim and report disk space"
  # remove 4GiB of images
  sudo systemd-run docker system prune --force --all --volumes

  # remove unused software
  sudo systemd-run --wait rm -rf \
    "$AGENT_TOOLSDIRECTORY" \
    /opt/* \
    /usr/local/* \
    /usr/share/az* \
    /usr/share/dotnet \
    /usr/share/gradle* \
    /usr/share/miniconda \
    /usr/share/swift \
    /var/lib/gems \
    /var/lib/mysql \
    /var/lib/snapd

  # trim the cleaned space
  sudo fstrim /

  # disk usage afterwards
  df -h /
  echo "::endgroup::"
}

case "$1" in
  build)
    prerun
    mod_build
    ;;
  tests)
    prerun
    mod_install
    ;;
esac
