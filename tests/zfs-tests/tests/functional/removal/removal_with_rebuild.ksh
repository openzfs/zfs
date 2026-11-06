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
# segment copies that fail to read, and without a checksum it does not
# overwrite one copy with another, while a resilver still copies the data
# to the spare.
#
# STRATEGY:
# 1. Create a pool on one vdev, write a file, add a second vdev and remove
#    the first one with 8K removal segments, so every 128K block of the file
#    is split into 16 segments.  With two different copies of each segment
#    during the rebuild, that is more combinations than
#    zfs_reconstruct_indirect_combinations_max.
# 2. With rebuild progress suspended, sequentially replace the second vdev
#    with a new vdev (replacing) or a hot spare (spare), so the new device
#    has none of the data yet.
# 3. Verify every block with zdb -bcc.
# 4. Let the rebuild finish, scrub, and check the pool and the file.
# 5. Rebuild the second vdev onto a hot spare, then scrub while reads of the
#    second vdev fail (io), or flip bits with checksum=off (corrupt).  Check
#    that the scrub rewrote the copies that failed to read (io).
# 6. With checksum=off, resilver the second vdev onto a hot spare.
# 7. Detach the second vdev and check the spare's copy of the file.
#

verify_runnable "global"

VDEV_A=$TEST_BASE_DIR/removal_rebuild_a
VDEV_B=$TEST_BASE_DIR/removal_rebuild_b
VDEV_NEW=$TEST_BASE_DIR/removal_rebuild_new
ZDBOUT=$TEST_BASE_DIR/removal_rebuild_zdb.out
typeset file digest

function cleanup
{
	zinject -c all
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $ORIG_REBUILD_SCRUB_ENABLED
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_NEW $ZDBOUT
}

#
# Create the pool with checksum=$1, write $file, and move it onto the second
# vdev with every block split.
#
function setup_split_pool
{
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B $VDEV_NEW
	log_must zpool create -f -O compression=off -O recordsize=128k \
	    -O checksum=$1 $TESTPOOL $VDEV_A
	file=$(get_prop mountpoint $TESTPOOL)/file
	log_must dd if=/dev/urandom of=$file bs=1M count=16
	digest=$(xxh128digest $file)

	log_must zpool add $TESTPOOL $VDEV_B
	log_must set_tunable32 REMOVE_MAX_SEGMENT 8192
	log_must zpool remove -w $TESTPOOL $VDEV_A
}

#
# $1: "replacing" to replace the second vdev with a new vdev, or "spare" to
# replace it with a hot spare.
#
function test_rebuild
{
	typeset type=$1

	setup_split_pool on

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	if [[ $type == "spare" ]]; then
		log_must zpool add $TESTPOOL spare $VDEV_NEW
	fi
	log_must zpool replace -s $TESTPOOL $VDEV_B $VDEV_NEW
	log_must is_pool_resilvering $TESTPOOL
	sync_pool $TESTPOOL true

	zdb -bcc $TESTPOOL >$ZDBOUT 2>&1
	typeset zdbstat=$?
	if [[ $zdbstat -ne 0 ]]; then
		grep -i error $ZDBOUT | head -20
		log_fail "$type: zdb -bcc failed with exit status $zdbstat" \
		    "during the rebuild"
	fi

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait -t resilver $TESTPOOL
	if [[ $type == "spare" ]]; then
		log_must zpool detach $TESTPOOL $VDEV_B
	else
		# The old vdev is detached asynchronously.
		typeset -i i=0
		while vdevs_in_pool $TESTPOOL $VDEV_B && ((i++ < 30)); do
			sleep 1
		done
		log_mustnot vdevs_in_pool $TESTPOOL $VDEV_B
	fi
	log_must zpool scrub -w $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_NEW $TESTPOOL
	log_must test "$(xxh128digest $file)" = "$digest"

	destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_NEW
}

#
# Replace the second vdev with a hot spare, optionally scrub with injected
# read errors on the second vdev, then detach it and check the file.
#
# $1: checksum property
# $2: "rebuild" for a sequential rebuild, or "resilver"
# $3: zinject error for reads of the second vdev during a scrub: "io",
#     "corrupt" (flip bits), or "none" to skip the scrub
#
function test_spare
{
	typeset checksum=$1 how=$2 error=$3

	setup_split_pool $checksum
	log_must zpool add $TESTPOOL spare $VDEV_NEW
	if [[ $how == "rebuild" ]]; then
		log_must zpool replace -s $TESTPOOL $VDEV_B $VDEV_NEW
	else
		log_must zpool replace $TESTPOOL $VDEV_B $VDEV_NEW
	fi
	log_must zpool wait -t resilver $TESTPOOL

	if [[ $error != "none" ]]; then
		log_must zinject -d $VDEV_B -e $error -T read -f 100 $TESTPOOL
		log_must zpool scrub -w $TESTPOOL
		typeset repaired=$(zpool status -jp $TESTPOOL | \
		    jq -r '.pools[].scan_stats.processed')
		log_must zinject -c all
		log_must zpool clear $TESTPOOL
		log_note "$error: scrub repaired $repaired bytes"
		if [[ $error == "io" ]]; then
			log_must test $repaired -ge $((16 * 1024 * 1024))
		fi
	fi

	log_must zpool detach $TESTPOOL $VDEV_B
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_NEW $TESTPOOL
	log_must test "$(xxh128digest $file)" = "$digest"

	destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_NEW
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
test_spare on rebuild io
test_spare off rebuild corrupt
test_spare off resilver none

log_pass "Split blocks of a removed vdev can be verified and repaired" \
    "under replacing and spare vdevs"
