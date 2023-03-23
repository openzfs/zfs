#!/usr/bin/env bash

set -eu

TDIR="/usr/share/zfs/zfs-tests/tests/functional"
echo -n "TODO="
case "$1" in
  part1)
    # ~1h 20m
    echo "cli_root"
    ;;
  part2)
    # ~1h
    ls $TDIR|grep '^[a-m]'|grep -v "cli_root"|xargs|tr -s ' ' ','
    ;;
  part3)
    # ~1h
    ls $TDIR|grep '^[n-qs-z]'|xargs|tr -s ' ' ','
    ;;
  part4)
    # ~1h
    ls $TDIR|grep '^r'|xargs|tr -s ' ' ','
    ;;
esac
