#!/usr/bin/env bash

######################################################################
# 6) configure and build openzfs modules
######################################################################

set -eu
set -o pipefail
cd $HOME/zfs

#OPTS="-T casenorm"
OPTS="-T functional"

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
