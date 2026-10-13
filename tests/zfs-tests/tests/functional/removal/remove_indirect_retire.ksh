#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	The indirect vdev left by a device removal becomes a hole once
#	nothing refers to it any more, and the pool stays consistent.
#
# STRATEGY:
#	1. Create a pool on a mirror with a special mirror, which takes all
#	   the metadata, and a filesystem.  Add a second normal mirror and
#	   write a file, whose data goes to both normal mirrors.
#	2. Remove the second normal mirror and verify that it became
#	   indirect.
#	3. Delete the file and wait for the indirect vdev to become a hole.
#	4. Verify that zdb finds the feature refcounts consistent and no
#	   leaked object, that device_removal and obsolete_counts are enabled
#	   again, and that a scrub finds no error.
#	5. Verify that this still holds after an export and import.
#	6. Add a third normal mirror and remove it before writing to it, and
#	   wait for it to become a hole as well.
#

verify_runnable "global"

typeset VDIR=$TEST_BASE_DIR/remove_indirect_retire

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
	restore_tunable CONDENSE_INDIRECT_OBSOLETE_PCT
	restore_tunable CONDENSE_MIN_MAPPING_BYTES
}

function check_retired # id
{
	typeset id=$1

	log_must test "$(top_vdev_type $TESTPOOL $id)" = "hole"
	log_must eval "zdb -d $TESTPOOL > $VDIR/zdb.out"
	log_must grep -q \
	    "Verified device_removal feature refcount of 0 is correct" \
	    $VDIR/zdb.out
	log_must grep -q \
	    "Verified indirect_refcount feature refcount of 0 is correct" \
	    $VDIR/zdb.out
	log_must zdb -b $TESTPOOL
	log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = \
	    "enabled"
	log_must test "$(get_pool_prop feature@obsolete_counts $TESTPOOL)" = \
	    "enabled"
}

log_assert "An indirect vdev that nothing refers to becomes a hole"
log_onexit cleanup

# Condense the mapping as soon as part of it is obsolete.
log_must save_tunable CONDENSE_INDIRECT_OBSOLETE_PCT
log_must save_tunable CONDENSE_MIN_MAPPING_BYTES
log_must set_tunable32 CONDENSE_INDIRECT_OBSOLETE_PCT 0
log_must set_tunable64 CONDENSE_MIN_MAPPING_BYTES 1

log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDIR/0 $VDIR/1 $VDIR/2 $VDIR/3 \
    $VDIR/4 $VDIR/5 $VDIR/s0 $VDIR/s1

# Metadata, which may stay unchanged for long, goes to the special mirror
# (vdev 1), so only the file data is on the mirror that gets removed.
log_must zpool create -f -O compression=off $TESTPOOL mirror $VDIR/0 $VDIR/1 \
    special mirror $VDIR/s0 $VDIR/s1
log_must zfs create $TESTPOOL/$TESTFS
typeset mntpnt
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must zpool add $TESTPOOL mirror $VDIR/2 $VDIR/3
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=32
sync_pool $TESTPOOL

log_must zpool remove $TESTPOOL mirror-2
log_must wait_for_removal $TESTPOOL
log_must test "$(top_vdev_type $TESTPOOL 2)" = "indirect"

log_must rm $mntpnt/file
log_must wait_for_top_vdev_type $TESTPOOL 2 hole $mntpnt
check_retired 2

log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
check_retired 2

log_must zpool add $TESTPOOL mirror $VDIR/4 $VDIR/5
log_must zpool remove $TESTPOOL mirror-3
log_must wait_for_removal $TESTPOOL
log_must wait_for_top_vdev_type $TESTPOOL 3 hole $mntpnt
check_retired 3

log_pass "An indirect vdev that nothing refers to becomes a hole"
