#!/usr/bin/env bash

######################################################################
# 5) load openzfs modules
######################################################################

set -eu
cd $HOME/zfs

function freebsd() {
  echo "##[group]Load modules"
  # when freebsd zfs is loaded, unload this one
  kldstat -n zfs 2>/dev/null && sudo kldunload zfs
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  sudo -E ./scripts/zfs.sh
  sudo dmesg
  sudo dmesg -c > /var/tmp/dmesg-module-load.txt
  echo "Loaded module: "
  sudo kldstat -n openzfs
  uname -a > /var/tmp/uname.txt
  echo "##[endgroup]"
}

function linux_forceload() {
  echo "Need to force the module loading!"
  # -f tells modprobe to ignore wrong version numbers
  sudo modprobe -v -f spl || echo "!! Loading module spl is failing !!"
  sudo modprobe -v -f zfs || echo "!! Loading module zfs is failing !!"
}

function linux() {
  echo "##[group]Load modules"
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  sudo -E ./scripts/zfs.sh
  test -d /proc/spl/kstat/zfs || linux_forceload
  sudo dmesg
  sudo dmesg -c > /var/tmp/dmesg-module-load.txt
  uname -a > /var/tmp/uname.txt
  echo "##[endgroup]"
}

export PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin"
case "$1" in
  freebsd*)
    freebsd
    ;;
  *)
    linux
    ;;
esac
