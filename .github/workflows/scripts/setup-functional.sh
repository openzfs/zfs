#!/usr/bin/env bash

set -eu

TDIR="/usr/share/zfs/zfs-tests/tests/functional"
echo -n "TODO="
case "$1" in
  part1)
    # ~1h 10m
    echo "cli_root"
    ;;
  part2)
    # ~1h 30m
    ls $TDIR|grep '^[a-m]'|grep -v "cli_root"|xargs|tr -s ' ' ','
    ;;
  part3)
    # ~40m
    ls $TDIR|grep '^[n-qs-z]'|xargs|tr -s ' ' ','
    ;;
  part4)
    # ~1h
    ls $TDIR|grep '^r'|xargs|tr -s ' ' ','
    ;;
esac
