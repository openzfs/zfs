#!/usr/bin/env bash

######################################################################
# 6) configure and build openzfs modules
######################################################################

set -eu
set -o pipefail
cd $HOME/zfs

# OPTS="-T casenorm"
OPTS=""
OPTS="-T pool_checkpoint"

# Our VM has already created some ramdisks for us to use
# if [ -e /dev/vdb ] && [ -e /dev/vdc ] && [ -e /dev/vdd ] ; then
#     DISKS="/dev/vdb /dev/vdc /dev/vdd"
#     export DISKS
# elif [ -e /dev/vtbd1 ] && [ -e /dev/vtbd2 ] && [ -e /dev/vtbd3 ] ; then
#     DISKS="/dev/vtbd1 /dev/vtbd2 /dev/vtbd3"
#     export DISKS
# else
#     echo "no pre-created disks"
# fi

case "$1" in
  freebsd*)
    /usr/local/share/zfs/zfs-tests.sh -vKR -s 3G $OPTS \
      | scripts/zfs-tests-color.sh
    ;;
  *)
    /usr/share/zfs/zfs-tests.sh -vKR -s 3G $OPTS \
      | scripts/zfs-tests-color.sh
    ;;
esac
