#!/usr/bin/env bash

######################################################################
# 6) load openzfs module and run the tests
#
# called on runner:  qemu-6-tests.sh (without extra args)
# called on qemu-vm: qemu-6-tests.sh $OS $2/$3
######################################################################

set -o pipefail

# called directly on the runner
if [ -f /var/tmp/vms.txt ]; then
  cd "/var/tmp"

  OS=`cat os.txt`
  VMs=`cat vms.txt`
  SSH=`which ssh`

  BASE="$HOME/work/zfs/zfs"
  TESTS='$HOME/zfs/.github/workflows/scripts/qemu-6-tests.sh'
  COLOR="$BASE/scripts/zfs-tests-color.sh"

  # df statistics - keep an eye on disk usage
  echo "Disk usage before:" > disk-usage.txt
  df -h /mnt/tests >> disk-usage.txt

  for i in `seq 1 $VMs`; do
    IP="192.168.122.1$i"
    daemonize -c /var/tmp -p vm${i}.pid -o vm${i}log.txt -- \
      $SSH zfs@$IP $TESTS $OS $i $VMs
    # give us the output of stdout + stderr - with prefix ;)
    tail -fq vm${i}log.txt | $COLOR | sed -e "s/^/vm${i}: /g" &
    echo $! > vm${i}log.pid
  done


  # wait for all vm's to finish
  for i in `seq 1 $VMs`; do
    tail --pid=`cat vm${i}.pid` -f /dev/null
    pid=`cat vm${i}log.pid`
    rm -f vm${i}log.pid
    kill $pid
  done

  # df statistics part 2
  echo "Disk usage afterwards:" >> disk-usage.txt
  df -h /mnt/tests >> disk-usage.txt
  echo "VM files take this space:" >> disk-usage.txt
  du -sh /mnt/tests >> disk-usage.txt

  exit 0
fi

# this part runs inside qemu vm
export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
case "$1" in
  freebsd*)
    # when freebsd's zfs is loaded, unload this one
    sudo kldstat -n zfs 2>/dev/null && sudo kldunload zfs
    sudo -E ./zfs/scripts/zfs.sh
    sudo dmesg -c > /var/tmp/dmesg-prerun.txt
    TDIR="/usr/local/share/zfs"
    ;;
  *)
    sudo -E modprobe zfs
    sudo dmesg -c > /var/tmp/dmesg-prerun.txt
    TDIR="/usr/share/zfs"
    ;;
esac

# run functional testings
TAGS=$2/$3
#TAGS=casenorm,zpool_trim,trim,raidz

$TDIR/zfs-tests.sh -vK -s 3G -T $TAGS
RV=$?

# we wont fail here, this will be done later
echo $RV > /var/tmp/exitcode.txt
exit 0
