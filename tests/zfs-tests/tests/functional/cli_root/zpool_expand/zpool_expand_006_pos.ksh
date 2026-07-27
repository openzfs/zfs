#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Andrew Mochalskyi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# A pool on a whole-disk vdev autoexpands from the kernel's own
# capacity change event alone (issue #12505).
#
# STRATEGY:
# 1) Create a pool with autoexpand=on on a single scsi_debug device,
#    so zfs owns the whole disk and puts a partition table on it
# 2) Grow the disk and let the SCSI layer pick up the new capacity
# 3) Wait for the pool to expand
#
# The kernel reports a resize with a single change event on the disk;
# nothing is generated for the partitions.  zpool_expand_001_pos
# passes this point on an unpatched zed only because
# block_device_wait runs a bare udevadm trigger, which resends change
# events for the zfs_member partitions and hands zed the vdev guid
# the resize never delivered.  For the same reason the udev events
# left over from pool creation must be drained before growing the
# disk: only udevadm settle may be used after that point, so that zed
# sees the resize event and nothing else, just like with a disk grown
# in production.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	unload_scsi_debug
}

# Wait for the pool size to grow past $1.
function wait_for_autoexpand
{
	typeset prev_size=$1
	typeset -i i

	for (( i = 0; i < 60; i++ )); do
		typeset new_size=$(get_pool_prop size $TESTPOOL1)
		if [[ $new_size -gt $prev_size ]]; then
			return
		fi
		sleep 1
	done
	log_fail "$TESTPOOL1 never grew past $prev_size (still $new_size)"
}

log_onexit cleanup

log_assert "whole-disk pool autoexpands from the disk's own resize event"

MINVDEVSIZE_MB=$((MINVDEVSIZE / 1048576))
load_scsi_debug $MINVDEVSIZE_MB 1 1 1 '512b'
block_device_wait
DEV=$(get_debug_device)

log_must zpool create -o autoexpand=on $TESTPOOL1 $DEV

# Let the udev fallout of the pool creation (partition table writes,
# device node closes) fully play out before the resize, so the only
# event left for zed to see below is the resize itself.
udevadm settle
sleep 2
udevadm settle

typeset prev_size=$(get_pool_prop size $TESTPOOL1)

# Grow the device and have the SCSI disk driver re-read the capacity.
# This makes the kernel emit a change event with RESIZE=1 for the
# disk, and nothing for its partitions.
echo "2" > /sys/bus/pseudo/drivers/scsi_debug/virtual_gb
echo "1" > /sys/class/block/$DEV/device/rescan
udevadm settle

wait_for_autoexpand $prev_size

log_note "$TESTPOOL1 grew from $prev_size to $(get_pool_prop size $TESTPOOL1)"

log_pass "whole-disk pool autoexpanded from the disk's own resize event"
