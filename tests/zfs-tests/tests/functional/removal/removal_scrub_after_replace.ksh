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

#
# Copyright (c) 2026 by Oxide Computer Company. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# A scrub that runs while the new device of a "zpool replace" is missing
# data, instead of a resilver, must write the segments of split blocks of a
# removed vdev to the new device.  When the scrub completes the new device
# is considered complete, so after the old device is detached it holds the
# only copy.
#
# STRATEGY:
# 1. Create a pool, write a file, add a second top-level vdev and remove
#    the first one with 8K removal segments, so every 128K block of the
#    file is split into 16 segments on the second vdev.
# 2. With the scan suspended, replace a leaf of the second vdev with a new
#    vdev or a hot spare.  Export the pool, move the new device's file away
#    and import the pool, so that the resilver completes without the new
#    device, which is left without the data.
# 3. Move the file back and scrub.  The scrub reopens the new device, which
#    doesn't schedule a resilver.  Wait for the scrub, check that a scrub
#    (not a resilver) ran, and detach the old leaf.
# 4. Re-import the pool with the new vdev holding the only copy of the
#    file's segments, check the file, scrub and check for errors.
# 5. Do this for a single-disk vdev replaced by a new vdev, the same with a
#    hot spare, the same without checksums, the second child of a mirror
#    replaced by a new vdev, and the first child of a mirror replaced by a
#    new vdev and overwritten before the scrub.
# 6. Do it with two top-level vdevs that have segments of the same blocks:
#    a child of one of two mirrors replaced by a new vdev; a single disk
#    replaced by a new vdev and then a child of the mirror next to it by a
#    hot spare; and a child of a mirror that has a hot spare in use replaced
#    by a new vdev.
# 7. Do it for the second child of a mirror when both children are damaged
#    in different segments of every block and the scrub tries few of the
#    combinations of segments, so that it can't reconstruct some blocks.
#    Check that the file can be read once all combinations are tried.
#
verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/removal_scrub_after_replace
# Outside $VDEV_DIR, which the pool is imported from.
typeset VDEV_HIDDEN=$VDEV_DIR.hidden
typeset VDEV_A=$VDEV_DIR/a
typeset VDEV_B1=$VDEV_DIR/b1
typeset VDEV_B2=$VDEV_DIR/b2
typeset VDEV_C1=$VDEV_DIR/c1
typeset VDEV_C2=$VDEV_DIR/c2
typeset VDEV_NEW=$VDEV_DIR/new
typeset VDEV_SPARE=$VDEV_DIR/spare

function cleanup
{
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable64 METASLAB_ALIQUOT $ORIG_METASLAB_ALIQUOT
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable32 RECONSTRUCT_INDIRECT_COMBINATIONS_MAX \
	    $ORIG_COMBINATIONS_MAX
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR $VDEV_HIDDEN
}

#
# Create the pool and its file, and move the file onto the vdev given by
# the arguments, with every block split.
#
function setup_split_pool
{
	log_must mkdir -p $VDEV_DIR
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B1 $VDEV_B2 $VDEV_C1 \
	    $VDEV_C2 $VDEV_NEW $VDEV_SPARE
	create_split_pool $VDEV_A "$@"
}

#
# Overwrite one sector in every 128K of vdev $1, at offset $2.
#
function damage_vdev
{
	typeset vdev=$1
	typeset -i off=$((4 * 1024 * 1024 + $2))
	typeset -i end=$(($(stat_size $vdev) - 4 * 1024 * 1024))

	while ((off < end)); do
		dd if=/dev/urandom of=$vdev bs=512 count=1 conv=notrunc \
		    seek=$((off / 512)) 2>/dev/null || \
		    log_fail "Failed to overwrite $vdev at $off"
		((off += 128 * 1024))
	done
}

#
# Overwrite a sector of every block on each child of the mirror, in
# different segments.  Then lower the limit on the combinations of segments
# that a read tries; the caller restores it.  The limit is lowered after
# the import, so that the import can reconstruct split metadata blocks that
# were damaged.
#
function damage_mirror_and_limit
{
	log_must zpool export $TESTPOOL
	damage_vdev $VDEV_B1 0
	damage_vdev $VDEV_B2 8192
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	log_must set_tunable32 RECONSTRUCT_INDIRECT_COMBINATIONS_MAX 1
}

#
# Replace $1 with $2 so that a scrub, not the replace's resilver, copies the
# data to $2.  The remaining arguments are a command that damages vdevs.  It
# is run after the resilver, which would otherwise repair the damage.
#
function scrub_during_replace # old new [command [args]]
{
	typeset old=$1
	typeset new=$2
	shift 2

	#
	# Keep the resilver from issuing I/O until the new vdev's file has
	# been moved away.  It then completes while the new vdev can't be
	# opened, so the new vdev gets none of the data and keeps its DTL.
	#
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace $TESTPOOL $old $new
	log_must zpool export $TESTPOOL
	log_must mv $new $VDEV_HIDDEN
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver $TESTPOOL
	# The resilver didn't complete the replace.
	log_must vdevs_in_pool $TESTPOOL $old

	if (($# > 0)); then
		"$@"
	fi

	#
	# The scrub reopens the new vdev, which doesn't schedule a resilver.
	# The reopen of a "zpool online" or "zpool clear" would.
	#
	log_must mv $VDEV_HIDDEN $new
	log_must zpool scrub $TESTPOOL

	log_must zpool wait -t scrub,resilver,replace $TESTPOOL
	# A scheduled resilver would have run instead of the scrub.
	log_must is_pool_scrubbed $TESTPOOL true

	# A spare stays attached until the old leaf is detached.
	if vdevs_in_pool $TESTPOOL $old; then
		log_must zpool detach $TESTPOOL $old
	fi
	log_mustnot vdevs_in_pool $TESTPOOL $old
}

#
# Re-import the pool with $@ offline, so that the new vdev's copy of the
# file is the only one, and check the file and the pool.
#
function check_new_copy
{
	typeset vdev

	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	for vdev in "$@"; do
		log_must zpool offline $TESTPOOL $vdev
	done
	check_split_pool

	destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

log_assert "A scrub instead of a resilver writes split blocks of a removed" \
    "vdev to the new device"

ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
ORIG_METASLAB_ALIQUOT=$(get_tunable METASLAB_ALIQUOT)
ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_COMBINATIONS_MAX=$(get_tunable RECONSTRUCT_INDIRECT_COMBINATIONS_MAX)
log_onexit cleanup

log_note "Replace a single-disk vdev with a new vdev"
setup_split_pool $VDEV_B1
scrub_during_replace $VDEV_B1 $VDEV_NEW
check_new_copy

log_note "Replace a single-disk vdev with a hot spare"
setup_split_pool $VDEV_B1 spare $VDEV_NEW
scrub_during_replace $VDEV_B1 $VDEV_NEW
check_new_copy

log_note "Replace a single-disk vdev with a new vdev, without checksums"
SPLIT_POOL_OPTS="-O checksum=off"
setup_split_pool $VDEV_B1
SPLIT_POOL_OPTS=""
scrub_during_replace $VDEV_B1 $VDEV_NEW
check_new_copy

log_note "Replace the second child of a mirror with a new vdev"
setup_split_pool mirror $VDEV_B1 $VDEV_B2
scrub_during_replace $VDEV_B2 $VDEV_NEW
check_new_copy $VDEV_B1

#
# The scrub has to take every segment from the mirror's second child.  The
# copies of a segment are on the old vdev, the new vdev and the second child,
# in that order.  The old vdev's copy is bad, and the new vdev's copy is left
# out because the new vdev is missing the data according to its DTL.
#
log_note "Replace the first child of a mirror with a new vdev and overwrite" \
    "the old vdev before the scrub"
setup_split_pool mirror $VDEV_B1 $VDEV_B2
scrub_during_replace $VDEV_B1 $VDEV_NEW \
    damage_split_pool $VDEV_DIR $VDEV_B1
check_new_copy $VDEV_B2

SPLIT_POOL_SPREAD=1

log_note "Two mirrors: replace a child of one with a new vdev"
setup_split_pool mirror $VDEV_B1 $VDEV_B2 mirror $VDEV_C1 $VDEV_C2
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
scrub_during_replace $VDEV_B2 $VDEV_NEW
check_new_copy $VDEV_B1

log_note "A single disk and a mirror: replace the disk with a new vdev," \
    "then a child of the mirror with a hot spare"
setup_split_pool $VDEV_C1 mirror $VDEV_B1 $VDEV_B2 spare $VDEV_SPARE
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
scrub_during_replace $VDEV_C1 $VDEV_NEW
log_must zpool export $TESTPOOL
log_must zpool import -d $VDEV_DIR $TESTPOOL
check_split_pool
scrub_during_replace $VDEV_B2 $VDEV_SPARE
check_new_copy $VDEV_B1

log_note "Two mirrors: replace a child that has a hot spare in use with a" \
    "new vdev"
setup_split_pool mirror $VDEV_B1 $VDEV_B2 mirror $VDEV_C1 $VDEV_C2 \
    spare $VDEV_SPARE
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
log_must zpool replace $TESTPOOL $VDEV_B2 $VDEV_SPARE
log_must zpool wait -t resilver $TESTPOOL
scrub_during_replace $VDEV_B2 $VDEV_NEW
# The spare is released once the device that it spared is replaced.
log_must wait_hotspare_state $TESTPOOL $VDEV_SPARE "AVAIL"
check_new_copy $VDEV_B1

SPLIT_POOL_SPREAD=""

#
# Each child of the mirror has a bad sector in a different segment of every
# block, so a block is only good in a combination of segments from both
# children.  With the limit set to one combination, the scrub tries the one
# that takes every segment from the first child, and then a random one.
# Where it doesn't find the good one, it has to give the new vdev the old
# vdev's segments without verifying them.
#
log_note "Replace the second child of a mirror when blocks can't be" \
    "reconstructed"
setup_split_pool mirror $VDEV_B1 $VDEV_B2
scrub_during_replace $VDEV_B2 $VDEV_NEW damage_mirror_and_limit
log_must set_tunable32 RECONSTRUCT_INDIRECT_COMBINATIONS_MAX \
    $ORIG_COMBINATIONS_MAX
log_mustnot eval "zpool status $TESTPOOL | grep -q 'with 0 errors'"
check_new_copy

log_pass "A scrub instead of a resilver writes split blocks of a removed" \
    "vdev to the new device"
