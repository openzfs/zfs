#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Removing a special device from a pool succeeds.
#

verify_runnable "global"

#
# Verify the file identified by the input <inode> is written on a special vdev
# According to the pool layout used in this test vdev_id 3 and 4 are special
# XXX: move this function to libtest.shlib once we get "Vdev Properties"
#
function file_in_special_vdev # <dataset> <inode>
{
	typeset dataset="$1"
	typeset inum="$2"

	zdb -dddddd $dataset $inum | awk '{
# find DVAs from string "offset level dva" only for L0 (data) blocks
if (match($0,"L0 [0-9]+")) {
   dvas[0]=$3
   dvas[1]=$4
   dvas[2]=$5
   for (i = 0; i < 3; ++i) {
      if (match(dvas[i],"([^:]+):.*")) {
         dva = substr(dvas[i], RSTART, RLENGTH);
         # parse DVA from string "vdev:offset:asize"
         if (split(dva,arr,":") != 3) {
            print "Error parsing DVA: <" dva ">";
            exit 1;
         }
         # verify vdev is "special"
         if (arr[1] < 3) {
            exit 1;
         }
      }
   }
}}'
}

claim="Removing a special device from a pool succeeds."

log_assert $claim
log_onexit cleanup

#
# Create a non-raidz pool so we can remove top-level vdevs
#
log_must disk_setup
log_must zpool create $TESTPOOL $ZPOOL_DISK0 $ZPOOL_DISK1 $ZPOOL_DISK2 \
  special $CLASS_DISK0 special $CLASS_DISK1
log_must display_status "$TESTPOOL"

#
# Generate some metadata and small blocks in the special class before removal
#
typeset -l i=1
typeset -l blocks=25

log_must zfs create -o special_small_blocks=32K -o recordsize=32K \
	$TESTPOOL/$TESTFS
for i in 1 2 3 4; do
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/testfile.$i bs=1M \
	    count=$blocks
	((blocks = blocks + 25))
done
log_must sync_pool $TESTPOOL
log_must zpool list -v $TESTPOOL

# Verify the files were written in the special class vdevs
for i in 1 2 3 4; do
	dataset="$TESTPOOL/$TESTFS"
	inum="$(stat -c '%i' /$TESTPOOL/$TESTFS/testfile.$i)"
	log_must file_in_special_vdev $dataset $inum
done

#
# remove a special allocation vdev and force a remapping
# N.B. The 'zfs remap' command has been disabled and may be removed.
#
export ZFS_REMAP_ENABLED=YES

log_must zpool remove $TESTPOOL $CLASS_DISK0
log_must zfs remap $TESTPOOL/$TESTFS

sleep 5
log_must sync_pool $TESTPOOL
sleep 1

log_must zdb -bbcc $TESTPOOL
log_must zpool list -v $TESTPOOL
log_must zpool destroy -f "$TESTPOOL"

log_pass $claim
