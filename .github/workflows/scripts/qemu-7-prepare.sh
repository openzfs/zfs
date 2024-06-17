#!/usr/bin/env bash

######################################################################
# 7) prepare output of the results
######################################################################

set -o pipefail

cd /var/tmp
OS=`cat os.txt`

# check if building the module has failed
RESPATH="/var/tmp/test_results"
if [ ! -s vms.txt ]; then
  mkdir -p $RESPATH
  cd $RESPATH
  # build some simple summary:
  echo "!!! ZFS module didn't build successfully !!!" \
    | tee summary.txt | tee clean-summary.txt
  scp zfs@192.168.122.10:"/var/tmp/*.txt" $RESPATH || true
  cp -f /var/tmp/*.txt $RESPATH || true
  tar cf /tmp/qemu-$OS.tar -C $RESPATH -h . || true
  exit 0
fi

# build was okay
VMs=`cat vms.txt`

####################################################################
# vm${N}log.txt   -> output of ssh/tail -> for merged summary.txt
#
# vm${N}/build-stderr.txt  -> two copies -> moved to one file
# vm${N}/dmesg-prerun.txt  -> dmesg output of vm start
# vm${N}/console.txt       -> serial output of vm
# vm${N}/uname.txt         -> output of uname -a on test vm
#
# vm${N}/current/log       -> if not there, kernel panic loading
# vm${N}/current/results   -> if not there, kernel panic testings
####################################################################

BASE="$HOME/work/zfs/zfs"
MERGE="$BASE/.github/workflows/scripts/merge_summary.awk"

# catch result files of testings
for i in `seq 1 $VMs`; do
  rsync -arL zfs@192.168.122.1$i:$RESPATH/current $RESPATH/vm$i || true
  scp zfs@192.168.122.1$i:"/var/tmp/*.txt" $RESPATH/vm$i || true
done
cp -f /var/tmp/*.txt $RESPATH || true
cd $RESPATH

# Save a list of all failed test logs for easy access
awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; }; \
  /\[SKIP\]|\[PASS\]/{ show=0; } show' \
  vm*/current/log >> summary-failure-logs.txt

$MERGE vm*log.txt > summary-clean.txt
$MERGE vm*log.txt | $BASE/scripts/zfs-tests-color.sh > summary.txt

# we should have "vm count" identical build-stderr.txt files, need only one
for i in `seq 1 $VMs`; do
  file="vm$i/build-stderr.txt"
  test -s "$file" && mv -f $file build-stderr.txt
done

for i in `seq 1 $VMs`; do
  file="vm${i}log.txt"
  test -s $file && cat $file | $BASE/scripts/zfs-tests-color.sh > $file.color
done

# artifact ready now
tar cf /tmp/qemu-$OS.tar -C $RESPATH -h . || true
