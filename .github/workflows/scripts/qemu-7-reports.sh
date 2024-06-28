#!/usr/bin/env bash

######################################################################
# 7) output the results of the previous stage in an ordered way
######################################################################

set -o pipefail
cd /var/tmp

echo "VM disk usage before:"
cat disk-before.txt
echo "and afterwards:"
cat disk-afterwards.txt

FAIL="[1;91mFAIL[0m"
PASS="[92mPASS[0m"

BASE="$HOME/work/zfs/zfs"
MERGE="$BASE/.github/workflows/scripts/merge_summary.awk"
EXIT=0

msg2=$PASS
for i in `seq 1 3`; do
  f="exitcode.vm$i"
  scp 2>/dev/null zfs@192.168.122.1$i:/var/tmp/exitcode.txt $f
  test -f $f || echo 2 > $f
  rv=`cat $f`
  if [ $rv != 0 ]; then
    msg=$FAIL
    msg2=$FAIL
    EXIT=$rv
  else
    msg=$PASS
  fi

  echo "##[group]Results vm$i [$msg]"
  cat "vm${i}log.txt" | $BASE/scripts/zfs-tests-color.sh
  echo "##[endgroup]"
done

echo "##[group]Summary [$msg2]"
$MERGE vm{1,2,3}log.txt | $BASE/scripts/zfs-tests-color.sh
echo "##[endgroup]"

exit $EXIT
