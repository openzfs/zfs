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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A log vdev, or a top-level vdev that holds no data, is removed by turning
# it into a hole.  Its labels must be erased only once the txg that writes
# the hole has synced: an import that scans the devices ignores an erased
# label, so a crash in between would leave a pool whose config still has
# the vdev while its devices no longer show it.
#
# STRATEGY:
# 1. Create a pool of one disk and a log vdev.  Slow down the writes to the
#    disk with zinject, so that the txg that writes the hole takes seconds
#    to sync.
# 2. Remove the log vdev.  While the command runs, read the labels of both
#    devices with zdb, and verify that once the labels of the log vdev are
#    erased, those of the disk have the hole.
# 3. Do the same with an empty disk that was added to the pool.
#

verify_runnable "global"

DISKDIR=$(mktemp -d -p $TEST_BASE_DIR)
DISK=$DISKDIR/disk
VDEV=$DISKDIR/vdev
DELAY_MS=50

function cleanup
{
	zinject -c all
	wait
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DISKDIR
}

# Print the txg of each distinct label of a device, 0 for an erased one.
function label_txgs # device
{
	zdb -l $1 | awk '$1 == "txg:" { print $2 }'
}

function labels_erased # device
{
	typeset txgs=$(label_txgs $1)

	[[ -n "$txgs" ]] && ! echo "$txgs" | grep -qv '^0$'
}

function has_hole # device
{
	zdb -l $1 | grep -q hole_array
}

function remove_and_check # vdev
{
	typeset vdev=$1

	log_mustnot labels_erased $vdev
	log_mustnot has_hole $DISK

	log_must zinject -d $DISK -D $DELAY_MS:1 -T write $TESTPOOL
	zpool remove $TESTPOOL $vdev &
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if labels_erased $vdev; then
			has_hole $DISK || log_fail "The labels of $vdev were" \
			    "erased before the hole was written"
		fi
		sleep 0.1
	done
	log_must wait $pid
	log_must zinject -c all

	log_must labels_erased $vdev
	log_must has_hole $DISK
	log_mustnot vdevs_in_pool $TESTPOOL $vdev
}

log_onexit cleanup

log_assert "The labels of a vdev removed as a hole are erased after the hole"

log_must mkfile $MINVDEVSIZE $DISK $VDEV

#
# 1. and 2. A log vdev.
#
log_must zpool create -f $TESTPOOL $DISK log $VDEV
log_must sync_pool $TESTPOOL
remove_and_check $VDEV
destroy_pool $TESTPOOL

#
# 3. An empty disk.
#
log_must zpool create -f $TESTPOOL $DISK
log_must zpool add -f $TESTPOOL $VDEV
log_must zpool set allocating=off $TESTPOOL $VDEV
log_must test "$(get_vdev_prop allocated $TESTPOOL $VDEV)" -eq 0
log_must sync_pool $TESTPOOL
remove_and_check $VDEV

log_pass "The labels of a vdev removed as a hole are erased after the hole"
