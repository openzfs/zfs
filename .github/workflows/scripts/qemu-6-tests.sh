#!/usr/bin/env bash

######################################################################
# 6) load openzfs module and run the tests
######################################################################

set -o pipefail

if [ -z "$1" ]; then
  # called directly on the runner
  P="/var/tmp"

  cd $P
  OS=`cat os.txt`
  IP1="192.168.122.11"
  IP2="192.168.122.12"
  IP3="192.168.122.13"

  df -h / /mnt > /var/tmp/disk-before.txt

  # start as daemon and log stdout
  SSH=`which ssh`
  CMD='$HOME/zfs/.github/workflows/scripts/qemu-6-tests.sh'
  daemonize -c $P -p vm1.pid -o vm1log.txt -- \
    $SSH zfs@$IP1 $CMD $OS part1
  daemonize -c $P -p vm2.pid -o vm2log.txt -- \
    $SSH zfs@$IP2 $CMD $OS part2
  daemonize -c $P -p vm3.pid -o vm3log.txt -- \
    $SSH zfs@$IP3 $CMD $OS part3

  # give us the output of stdout + stderr - with prefix ;)
  BASE="$HOME/work/zfs/zfs"
  CMD="$BASE/scripts/zfs-tests-color.sh"
  tail -fq vm1log.txt | $CMD | sed -e "s/^/vm1: /g" &
  tail -fq vm2log.txt | $CMD | sed -e "s/^/vm2: /g" &
  tail -fq vm3log.txt | $CMD | sed -e "s/^/vm3: /g" &

  # wait for all vm's to finnish
  tail --pid=`cat vm1.pid` -f /dev/null
  tail --pid=`cat vm2.pid` -f /dev/null
  tail --pid=`cat vm3.pid` -f /dev/null

  # kill the tail/sed combo
  killall tail
  df -h / /mnt > /var/tmp/disk-afterwards.txt
  exit 0
fi

function freebsd() {
  # when freebsd zfs is loaded, unload this one
  kldstat -n zfs 2>/dev/null && sudo kldunload zfs
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  sudo -E ./scripts/zfs.sh
  sudo dmesg -c > /var/tmp/dmesg-module-load.txt
  sudo kldstat -n openzfs
}

function linux() {
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  sudo -E modprobe zfs
  sudo dmesg -c > /var/tmp/dmesg-module-load.txt
}

function gettests() {
  TF="$TDIR/zfs-tests/tests/functional"
  echo -n "-T "
  case "$1" in
    part1)
      # ~1h 40m (archlinux)
      echo "cli_root"
      ;;
    part2)
      # ~2h 5m (archlinux)
      ls $TF|grep '^[a-m]'|grep -v "cli_root"|xargs|tr -s ' ' ','
      ;;
    part3)
      # ~2h
      ls $TF|grep '^[n-z]'|xargs|tr -s ' ' ','
      ;;
  esac
}

function gettestsD() {
  TF="$TDIR/zfs-tests/tests/functional"
  echo -n "-T "
  case "$1" in
    part1)
      echo "checksum"
      ;;
    part2)
      echo "casenorm,trim"
      ;;
    part3)
      echo "zpool_add,zpool_create,zpool_export"
      ;;
  esac
}

# called within vm
export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"

case "$1" in
  freebsd*)
    TDIR="/usr/local/share/zfs"
    freebsd
    OPTS=`gettests $2`
    if [ -e /dev/vtbd1 ] && [ -e /dev/vtbd2 ] && [ -e /dev/vtbd3 ] ; then
      DISKS="/dev/vtbd1 /dev/vtbd2 /dev/vtbd3"
      export DISKS
    elif [ -e /dev/da1 ] && [ -e /dev/da2 ] && [ -e /dev/da3 ] ; then
      DISKS="/dev/da1 /dev/da2 /dev/da3"
      export DISKS
    fi
    ;;
  *)
    TDIR="/usr/share/zfs"
    OPTS=`gettests $2`
    linux
    if [ -e /dev/vdb ] && [ -e /dev/vdc ] && [ -e /dev/vdd ] ; then
      DISKS="/dev/vdb /dev/vdc /dev/vdd"
      export DISKS
    elif [ -e /dev/sdb ] && [ -e /dev/sdc ] && [ -e /dev/sdd ] ; then
      DISKS="/dev/sdb /dev/sdc /dev/sdd"
      export DISKS
    fi
    ;;
esac

# use loop devices by now:
# - the virtio disks have problems
# - virtual scsi disks have problems too
# - maybe some openzfs tests are to blame
unset DISKS

# this part runs inside qemu, finally: run tests
uname -a > /var/tmp/uname.txt
$TDIR/zfs-tests.sh -vKR -s 3G $OPTS
RV=$?
echo $RV > /var/tmp/exitcode.txt
exit $RV
