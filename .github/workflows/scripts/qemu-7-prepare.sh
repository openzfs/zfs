#!/usr/bin/env bash

######################################################################
# 7) prepare output of the results
# - this script pre-creates all needed logfiles for later summary
######################################################################

set -eu

# read our defined variables
cd /var/tmp
source env.txt

mkdir -p $RESPATH

# check if building the module has failed
if [ -z ${VMs:-} ]; then
  cd $RESPATH
  echo ":exclamation: ZFS module didn't build successfully :exclamation:" \
    | tee summary.txt | tee /tmp/summary.txt
  cp /var/tmp/*.txt .
  tar cf /tmp/qemu-$OS.tar -C $RESPATH -h . || true
  exit 0
fi

# build was okay
BASE="$HOME/work/zfs/zfs"
MERGE="$BASE/.github/workflows/scripts/merge_summary.awk"

# catch result files of testings (vm's should be there)
for i in $(seq 1 $VMs); do
  rsync -arL zfs@192.168.122.1$i:$RESPATH/current $RESPATH/vm$i || true
  scp zfs@192.168.122.1$i:"/var/tmp/*.txt" $RESPATH/vm$i || true
done
cp -f /var/tmp/*.txt $RESPATH || true
cd $RESPATH

# prepare result files for summary
for i in $(seq 1 $VMs); do
  file="vm$i/build-stderr.txt"
  test -s $file && mv -f $file build-stderr.txt

  file="vm$i/build-exitcode.txt"
  test -s $file && mv -f $file build-exitcode.txt

  file="vm$i/uname.txt"
  test -s $file && mv -f $file uname.txt

  file="vm$i/tests-exitcode.txt"
  if [ ! -s $file ]; then
    # XXX - add some tests for kernel panic's here
    # tail -n 80 vm$i/console.txt | grep XYZ
    echo 1 > $file
  fi
  rv=$(cat vm$i/tests-exitcode.txt)
  test $rv != 0 && touch /tmp/have_failed_tests

  file="vm$i/current/log"
  if [ -s $file ]; then
    cat $file >> log
    awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; }; \
      /\[SKIP\]|\[PASS\]/{ show=0; } show' \
      $file > /tmp/vm${i}dbg.txt
  fi

  file="vm${i}log.txt"
  fileC="/tmp/vm${i}log.txt"
  if [ -s $file ]; then
    cat $file >> summary
    cat $file | $BASE/scripts/zfs-tests-color.sh > $fileC
  fi
done

# create summary of tests
if [ -s summary ]; then
  $MERGE summary | grep -v '^/' > summary.txt
  $MERGE summary | $BASE/scripts/zfs-tests-color.sh > /tmp/summary.txt
  rm -f summary
else
  touch summary.txt /tmp/summary.txt
fi

# create file for debugging
if [ -s log ]; then
  awk '/\[FAIL\]|\[KILLED\]/{ show=1; print; next; }; \
    /\[SKIP\]|\[PASS\]/{ show=0; } show' \
    log > summary-failure-logs.txt
  rm -f log
else
  touch summary-failure-logs.txt
fi

# create debug overview for failed tests
cat summary.txt \
  | awk '/\(expected PASS\)/{ if ($1!="SKIP") print $2; next; } show' \
  | while read t; do
  cat summary-failure-logs.txt \
    | awk '$0~/Test[: ]/{ show=0; } $0~v{ show=1; } show' v="$t" \
    > /tmp/fail.txt
  SIZE=$(stat --printf="%s" /tmp/fail.txt)
  SIZE=$((SIZE/1024))
  # Test Summary:
  echo "##[group]$t ($SIZE KiB)" >> /tmp/failed.txt
  cat /tmp/fail.txt | $BASE/scripts/zfs-tests-color.sh >> /tmp/failed.txt
  echo "##[endgroup]" >> /tmp/failed.txt
  # Job Summary:
  echo -e "\n<details>\n<summary>$t ($SIZE KiB)</summary><pre>" >> failed.txt
  cat /tmp/fail.txt >> failed.txt
  echo "</pre></details>" >> failed.txt
done

if [ -e /tmp/have_failed_tests ]; then
  echo ":warning: Some tests failed!" >> failed.txt
else
  echo ":thumbsup: All tests passed." >> failed.txt
fi

if [ ! -s uname.txt ]; then
  echo ":interrobang: Panic - where is my uname.txt?" > uname.txt
fi

# artifact ready now
tar cf /tmp/qemu-$OS.tar -C $RESPATH -h . || true
