#!/usr/bin/env bash

######################################################################
# 6) load openzfs module and run the tests
#
# called on runner:  qemu-6-tests.sh
# called on qemu-vm: qemu-6-tests.sh $OS $2/$3
######################################################################

set -eu

function prefix() {
  ID="$1"
  LINE="$2"
  CURRENT=$(date +%s)
  TSSTART=$(cat /tmp/tsstart)
  DIFF=$((CURRENT-TSSTART))
  H=$((DIFF/3600))
  DIFF=$((DIFF-(H*3600)))
  M=$((DIFF/60))
  S=$((DIFF-(M*60)))

  CTR=$(cat /tmp/ctr)
  echo $LINE| grep -q "^Test[: ]" && CTR=$((CTR+1)) && echo $CTR > /tmp/ctr

  BASE="$HOME/work/zfs/zfs"
  COLOR="$BASE/scripts/zfs-tests-color.sh"
  CLINE=$(echo $LINE| grep "^Test[ :]" | sed -e 's|/usr/local|/usr|g' \
    | sed -e 's| /usr/share/zfs/zfs-tests/tests/| |g' | $COLOR)
  if [ -z "$CLINE" ]; then
    printf "vm${ID}: %s\n" "$LINE"
  else
    # [vm2: 00:15:54  256] Test: functional/checksum/setup (run as root) [00:00] [PASS]
    printf "[vm${ID}: %02d:%02d:%02d %4d] %s\n" \
      "$H" "$M" "$S" "$CTR" "$CLINE"
  fi
}

# called directly on the runner
if [ -z ${1:-} ]; then
  cd "/var/tmp"
  source env.txt
  SSH=$(which ssh)
  TESTS='$HOME/zfs/.github/workflows/scripts/qemu-6-tests.sh'
  echo 0 > /tmp/ctr
  date "+%s" > /tmp/tsstart

  for i in $(seq 1 $VMs); do
    IP="192.168.122.1$i"
    daemonize -c /var/tmp -p vm${i}.pid -o vm${i}log.txt -- \
      $SSH zfs@$IP $TESTS $OS $i $VMs $CI_TYPE
    # handly line by line and add info prefix
    stdbuf -oL tail -fq vm${i}log.txt \
      | while read -r line; do prefix "$i" "$line"; done &
    echo $! > vm${i}log.pid
    # don't mix up the initial --- Configuration --- part
    sleep 0.13
  done

  # wait for all vm's to finish
  for i in $(seq 1 $VMs); do
    tail --pid=$(cat vm${i}.pid) -f /dev/null
    pid=$(cat vm${i}log.pid)
    rm -f vm${i}log.pid
    kill $pid
  done

  exit 0
fi

# this part runs inside qemu vm
export PATH="$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
case "$1" in
  freebsd*)
    sudo kldstat -n zfs 2>/dev/null && sudo kldunload zfs
    sudo -E ./zfs/scripts/zfs.sh
    TDIR="/usr/local/share/zfs"
    ;;
  *)
    # use xfs @ /var/tmp for all distros
    sudo mv -f /var/tmp/*.txt /tmp
    sudo mkfs.xfs -fq /dev/vdb
    sudo mount -o noatime /dev/vdb /var/tmp
    sudo chmod 1777 /var/tmp
    sudo mv -f /tmp/*.txt /var/tmp
    sudo -E modprobe zfs
    TDIR="/usr/share/zfs"
    ;;
esac

# run functional testings and save exitcode
cd /var/tmp
TAGS=$2/$3
if [ "$4" == "quick" ]; then
  export RUNFILES="sanity.run"
fi
sudo dmesg -c > dmesg-prerun.txt
mount > mount.txt
df -h > df-prerun.txt
$TDIR/zfs-tests.sh -vK -s 3GB -T $TAGS
RV=$?
df -h > df-postrun.txt
echo $RV > tests-exitcode.txt
sync
exit 0
