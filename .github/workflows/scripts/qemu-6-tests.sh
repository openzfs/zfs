#!/usr/bin/env bash

######################################################################
# 6) load openzfs module and run the tests
#
# called via runner:
# - qemu-6-tests.sh $OS $VMs (freebsd14 3)
#
# called on qemu machine:
# - qemu-6-tests.sh $OS $VMcur $VMmax (freebsd14 1 3)
######################################################################

set -o pipefail

# called directly on the runner
if [ -z $3 ]; then
  OS=$1
  VMs=$2

  P="/var/tmp"
  SSH=`which ssh`
  BASE="$HOME/work/zfs/zfs"
  TESTS='$HOME/zfs/.github/workflows/scripts/qemu-6-tests.sh'
  COLOR="$BASE/scripts/zfs-tests-color.sh"

  cd $P
  df -h /mnt/tests > df-prerun.txt
  for i in `seq 1 $VMs`; do
    IP="192.168.122.1$i"
    daemonize -c $P -p vm${i}.pid -o vm${i}log.txt -- \
      $SSH zfs@$IP $TESTS $OS $i $VMs
    # give us the output of stdout + stderr - with prefix ;)
    tail -fq vm${i}log.txt | $COLOR | sed -e "s/^/vm${i}: /g" &
  done

  # wait for all vm's to finnish
  for i in `seq 1 $VMs`; do
      tail --pid=`cat vm${i}.pid` -f /dev/null
  done
  df -h /mnt/tests > df-postrun.txt

  du -sh /mnt/tests/openzfs.qcow2 >> df-postrun.txt
  for i in `seq 1 $VMs`; do
    du -sh /mnt/tests/vm${i}.qcow2 >> df-postrun.txt
  done

  # kill the tail/sed combo
  killall tail
  sleep 1
  exit 0
fi

function freebsd() {
  # when freebsd zfs is loaded, unload this one
  sudo kldstat -n zfs 2>/dev/null && sudo kldunload zfs
  sudo -E ./zfs/scripts/zfs.sh
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  TDIR="/usr/local/share/zfs"
}

function linux() {
  # remount rootfs with relatime + trim
  sudo mount -o remount,rw,relatime,discard /
  sudo -E modprobe zfs
  sudo dmesg -c > /var/tmp/dmesg-prerun.txt
  TDIR="/usr/share/zfs"
}

# called within vm
export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"

case "$1" in
  freebsd*)
    freebsd
    ;;
  *)
    linux
    ;;
esac

# this part runs inside qemu, finally: run tests
cd /var/tmp
uname -a > /var/tmp/uname.txt

# -R Automatically rerun failing tests
# -> this switch can not be used with our merging tool currently
#echo "RUNNING: $TDIR/zfs-tests.sh -vK -s 3G -T $2/$3"
$TDIR/zfs-tests.sh -vK -s 3G -T $2/$3

RV=$?
echo $RV > /var/tmp/exitcode.txt
exit $RV
