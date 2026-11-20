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
# Blocks of a removed vdev that were split into several segments can be
# verified while a segment's new location is being rebuilt onto a new
# replacing or spare device.  Once a spare is in place, a scrub rewrites
# segment copies that fail to read.
#
# STRATEGY:
# 1. Create a pool on one vdev, write a file, add a second vdev and remove
#    the first one with 8K removal segments, so every 128K block of the file
#    is split into 16 segments.
# 2. With rebuild progress suspended, sequentially replace the second vdev
#    with a new vdev (replacing) or a hot spare (spare), so the new device
#    has none of the data yet.
# 3. Verify every block with zdb -bcc.
# 4. Let the rebuild finish, scrub, and check the pool and the file.
# 5. Repeat, but cancel the rebuild by taking the new device offline, and
#    bring it back online while its file is moved away, so that no resilver
#    is scheduled.  Move the file back and scrub, so that the scrub runs
#    while the new device is missing data.  Verify every block with zdb
#    -bcc, let the scrub finish and check the file.
# 6. Rebuild the second vdev onto a hot spare, then scrub while reads of the
#    second vdev fail, and check that the scrub rewrote them.
# 7. Detach the second vdev and check the spare's copy of the file.
#
verify_runnable "global"

VDEV_A=$TEST_BASE_DIR/removal_rebuild_a
VDEV_B=$TEST_BASE_DIR/removal_rebuild_b
VDEV_NEW=$TEST_BASE_DIR/removal_rebuild_new
ZDBOUT=$TEST_BASE_DIR/removal_rebuild_zdb.out

function cleanup
{
	zinject -c all
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $ORIG_REBUILD_SCRUB_ENABLED
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_NEW $VDEV_NEW.hidden $ZDBOUT
}

#
# Create the pool and its file, and move the file onto the second vdev with
# every block split.
#
function setup_split_pool
{
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B $VDEV_NEW
	create_split_pool $VDEV_A $VDEV_B
}

#
# Verify every block with zdb -bcc.  $1 describes the state of the pool.
#
function check_zdb
{
	zdb -bcc $TESTPOOL >$ZDBOUT 2>&1
	typeset zdbstat=$?
	if ((zdbstat != 0)); then
		grep -i error $ZDBOUT | head -20
		log_fail "$1: zdb -bcc failed with exit status $zdbstat"
	fi
}

#
# $1: "replacing" to replace the second vdev with a new vdev, or "spare" to
# replace it with a hot spare.
#
function test_rebuild
{
	typeset type=$1

	setup_split_pool

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	if [[ $type == "spare" ]]; then
		log_must zpool add $TESTPOOL spare $VDEV_NEW
	fi
	log_must zpool replace -s $TESTPOOL $VDEV_B $VDEV_NEW
	log_must is_pool_resilvering $TESTPOOL

	check_zdb "$type: during the rebuild"

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait -t resilver,replace $TESTPOOL
	if [[ $type == "spare" ]]; then
		log_must zpool detach $TESTPOOL $VDEV_B
	fi
	log_mustnot vdevs_in_pool $TESTPOOL $VDEV_B
	log_must zpool scrub -w $TESTPOOL
	check_new_copy
}

#
# Like test_rebuild, but cancel the rebuild and let a scrub run instead of
# the resilver that follows it.  $1 is as for test_rebuild.
#
function test_canceled_rebuild
{
	typeset type=$1

	setup_split_pool

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	if [[ $type == "spare" ]]; then
		log_must zpool add $TESTPOOL spare $VDEV_NEW
	fi
	log_must zpool replace -s $TESTPOOL $VDEV_B $VDEV_NEW

	# With the new device offline, the rebuild is canceled.
	log_must zpool offline $TESTPOOL $VDEV_NEW
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver $TESTPOOL
	log_must eval "zpool status $TESTPOOL | grep -q 'resilver.*canceled'"

	#
	# Bring the new device back online while it can't be opened, so
	# that no resilver is scheduled.  The scrub reopens it, and that
	# reopen doesn't schedule a resilver either.
	#
	log_must mv $VDEV_NEW $VDEV_NEW.hidden
	log_must zpool online $TESTPOOL $VDEV_NEW
	log_must mv $VDEV_NEW.hidden $VDEV_NEW
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool scrub $TESTPOOL
	log_must is_pool_scrubbing $TESTPOOL

	check_zdb "$type: during a scrub after a canceled rebuild"

	# The scrub copies the data to the new device.
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait -t scrub,resilver,replace $TESTPOOL
	# A scheduled resilver would have run instead of the scrub.
	log_must is_pool_scrubbed $TESTPOOL true
	if [[ $type == "spare" ]]; then
		log_must zpool detach $TESTPOOL $VDEV_B
	fi
	log_mustnot vdevs_in_pool $TESTPOOL $VDEV_B
	log_must zpool scrub -w $TESTPOOL
	check_new_copy
}

#
# Check that the pool has no errors and that the file can be read from the
# new vdev alone, then destroy the pool.
#
function check_new_copy
{
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_NEW $TESTPOOL
	log_must test "$(xxh128digest $SPLIT_FILE)" = "$SPLIT_DIGEST"

	destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_NEW
}

#
# Rebuild the second vdev onto a hot spare, scrub while reads of the second
# vdev fail, then detach it and check the file.
#
function test_spare
{
	setup_split_pool
	log_must zpool add $TESTPOOL spare $VDEV_NEW
	log_must zpool replace -s $TESTPOOL $VDEV_B $VDEV_NEW
	log_must zpool wait -t resilver $TESTPOOL

	log_must zinject -d $VDEV_B -e io -T read -f 100 $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	typeset repaired=$(zpool status -jp $TESTPOOL | \
	    jq -r '.pools[].scan_stats.processed')
	log_must zinject -c all
	log_must zpool clear $TESTPOOL
	log_note "scrub repaired $repaired bytes"
	log_must test $repaired -ge $((16 * 1024 * 1024))

	log_must zpool detach $TESTPOOL $VDEV_B
	check_new_copy
}

log_assert "Split blocks of a removed vdev can be verified and repaired" \
    "under replacing and spare vdevs"

ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_REBUILD_SCRUB_ENABLED=$(get_tunable REBUILD_SCRUB_ENABLED)
log_onexit cleanup

# Scrub explicitly, after the rebuild, instead of when it finishes.
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

test_rebuild replacing
test_rebuild spare
test_canceled_rebuild replacing
test_canceled_rebuild spare
test_spare

log_pass "Split blocks of a removed vdev can be verified and repaired" \
    "under replacing and spare vdevs"
