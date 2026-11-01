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
#	The indirect vdev left by a device removal stays indirect while
#	something still refers to it, while zfs_retire_indirect_vdevs is 0
#	and while the pool has a checkpoint, and becomes a hole once that
#	is released.  Snapshots taken and destroyed afterwards drop the
#	lists of remapped blocks left over, and feature@obsolete_counts
#	returns to enabled.
#
# STRATEGY:
#	For each case below, create a pool on a mirror with a special mirror,
#	which takes all the metadata, add a second normal mirror, write a
#	file, remove the second normal mirror and delete the file.  Verify
#	that the indirect vdev stays while the holder is there, then release
#	it and wait for the vdev to become a hole.
#	1. zfs_retire_indirect_vdevs set to 0, then a checkpoint taken: both
#	   keep the vdev although its mapping has no entries left.  Set the
#	   tunable back to 1, then discard the checkpoint.
#	2. A second copy of the file deduplicated onto the same blocks.
#	3. A clone of the file's blocks.
#	4. A snapshot whose blocks were remapped after the removal.  The
#	   list of remapped blocks that the filesystem keeps for it outlives
#	   the vdev, and keeps feature@obsolete_counts active, until a
#	   snapshot is taken: take one and destroy it.
#	5. The same, with a snapshot taken after the remapping, which keeps
#	   that list after the vdev is retired: destroy it.
#	After cases 4 and 5, verify that feature@obsolete_counts is enabled.
#

verify_runnable "global"

typeset VDIR=$TEST_BASE_DIR/remove_indirect_retire_held
typeset MNT

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
	restore_tunable CONDENSE_INDIRECT_OBSOLETE_PCT
	restore_tunable CONDENSE_MIN_MAPPING_BYTES
	restore_tunable RETIRE_INDIRECT_VDEVS
	restore_tunable BCLONE_ENABLED
}

#
# The normal mirror added after the pool is created, vdev 2, only gets
# file data: metadata, which may stay unchanged for long, goes to the
# special mirror, vdev 1.
#
function setup_pool # filesystem options
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must truncate -s $MINVDEVSIZE $VDIR/0 $VDIR/1 $VDIR/2 $VDIR/3 \
	    $VDIR/s0 $VDIR/s1
	log_must zpool create -f -O compression=off $TESTPOOL \
	    mirror $VDIR/0 $VDIR/1 special mirror $VDIR/s0 $VDIR/s1
	log_must zfs create "$@" $TESTPOOL/$TESTFS
	MNT=$(get_prop mountpoint $TESTPOOL/$TESTFS)
	log_must zpool add $TESTPOOL mirror $VDIR/2 $VDIR/3
	log_must dd if=/dev/urandom of=$MNT/file bs=1M count=16
	sync_pool $TESTPOOL
}

function remove_mirror
{
	log_must zpool remove $TESTPOOL mirror-2
	log_must wait_for_removal $TESTPOOL
	log_must test "$(top_vdev_type $TESTPOOL 2)" = "indirect"
}

#
# Waits until the mapping of the indirect vdev has no entries, which
# zpool status shows by no longer reporting memory used for it.
#
function wait_for_empty_mapping
{
	typeset -i i

	for ((i = 0; i < 120; i++)); do
		zpool status $TESTPOOL | \
		    grep -q "memory used for removed device mappings" || \
		    return 0
		sync_pool $TESTPOOL
		sleep 0.5
	done
	zpool status $TESTPOOL
	return 1
}

function check_held
{
	log_mustnot wait_for_top_vdev_type $TESTPOOL 2 hole $MNT 20
}

function check_retired
{
	log_must wait_for_top_vdev_type $TESTPOOL 2 hole $MNT
	log_must eval "zdb -d $TESTPOOL > $VDIR/zdb.out"
	log_must grep -q \
	    "Verified device_removal feature refcount of 0 is correct" \
	    $VDIR/zdb.out
	log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = \
	    "enabled"
}

#
# Waits until feature@obsolete_counts is no longer active, then verifies
# its refcount.
#
function check_counts_dropped
{
	typeset -i i

	for ((i = 0; i < 20; i++)); do
		[[ $(get_pool_prop feature@obsolete_counts $TESTPOOL) == \
		    "enabled" ]] && break
		sync_pool $TESTPOOL
	done
	log_must test "$(get_pool_prop feature@obsolete_counts $TESTPOOL)" = \
	    "enabled"
	log_must eval "zdb -d $TESTPOOL > $VDIR/zdb.out"
	log_must grep -q \
	    "Verified indirect_refcount feature refcount of 0 is correct" \
	    $VDIR/zdb.out
}

log_assert "An indirect vdev stays while something holds it"
log_onexit cleanup

# Condense the mapping as soon as part of it is obsolete.
log_must save_tunable CONDENSE_INDIRECT_OBSOLETE_PCT
log_must save_tunable CONDENSE_MIN_MAPPING_BYTES
log_must save_tunable RETIRE_INDIRECT_VDEVS
log_must set_tunable32 CONDENSE_INDIRECT_OBSOLETE_PCT 0
log_must set_tunable64 CONDENSE_MIN_MAPPING_BYTES 1
if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

log_must mkdir -p $VDIR

log_note "zfs_retire_indirect_vdevs unset, then a checkpoint"
log_must set_tunable32 RETIRE_INDIRECT_VDEVS 0
setup_pool
remove_mirror
log_must rm $MNT/file
log_must wait_for_empty_mapping
check_held
log_must zpool checkpoint $TESTPOOL
log_must set_tunable32 RETIRE_INDIRECT_VDEVS 1
check_held
log_must zpool checkpoint -d -w $TESTPOOL
check_retired

log_note "A deduplicated copy"
setup_pool -o dedup=on
log_must dd if=$MNT/file of=$MNT/copy bs=1M
sync_pool $TESTPOOL
remove_mirror
log_must rm $MNT/file
check_held
log_must rm $MNT/copy
check_retired

log_note "A block clone"
setup_pool
log_must clonefile -f $MNT/file $MNT/clone
sync_pool $TESTPOOL
log_must test "$(get_pool_prop bcloneused $TESTPOOL)" != "0"
remove_mirror
log_must rm $MNT/file
check_held
log_must rm $MNT/clone
check_retired

log_note "A snapshot of remapped blocks"
setup_pool -o recordsize=128k
log_must zfs snapshot $TESTPOOL/$TESTFS@snap
remove_mirror
# Rewriting one block dirties the indirect block above the others, which
# remaps them while the snapshot still refers to their old location.
log_must dd if=/dev/urandom of=$MNT/file bs=128k count=1 conv=notrunc
sync_pool $TESTPOOL
log_must rm $MNT/file
check_held
log_must zfs destroy $TESTPOOL/$TESTFS@snap
check_retired
log_must zfs snapshot $TESTPOOL/$TESTFS@after
log_must zfs destroy $TESTPOOL/$TESTFS@after
check_counts_dropped

log_note "A snapshot of remapped blocks, then one of the remapping"
setup_pool -o recordsize=128k
log_must zfs snapshot $TESTPOOL/$TESTFS@snap
remove_mirror
log_must dd if=/dev/urandom of=$MNT/file bs=128k count=1 conv=notrunc
sync_pool $TESTPOOL
log_must zfs snapshot $TESTPOOL/$TESTFS@remapped
log_must rm $MNT/file
check_held
log_must zfs destroy $TESTPOOL/$TESTFS@snap
check_retired
log_must test "$(get_pool_prop feature@obsolete_counts $TESTPOOL)" = "active"
log_must zfs destroy $TESTPOOL/$TESTFS@remapped
check_counts_dropped

log_pass "An indirect vdev stays while something holds it"
