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
# Copyright (c) 2026, Christos Longros. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool create' should report which device is in use when it fails
# because a vdev belongs to an active pool.
#
# STRATEGY:
# 1. Create a backing file for two block devices.
# 2. Attach two block devices to the same file.
# 3. Attempt to create a mirror pool using both devices.
# 4. Verify the error message identifies the specific device.
# 5. Verify the error message names the active pool.
#

verify_runnable "global"

TESTFILE="$TEST_BASE_DIR/vdev_errinfo"
TESTPOOL2="testpool_errinfo"
BLKDEV1=""
BLKDEV2=""

function cleanup
{
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL

	if is_linux; then
		[[ -n "$BLKDEV1" ]] && losetup -d "$BLKDEV1" 2>/dev/null
		[[ -n "$BLKDEV2" ]] && losetup -d "$BLKDEV2" 2>/dev/null
	elif is_freebsd; then
		[[ -n "$BLKDEV1" ]] && mdconfig -d -u "$BLKDEV1" 2>/dev/null
		[[ -n "$BLKDEV2" ]] && mdconfig -d -u "$BLKDEV2" 2>/dev/null
	fi

	rm -f "$TESTFILE"
}

log_assert "'zpool create' reports device-specific errors for in-use vdevs."
log_onexit cleanup

# Create a file to back the block devices
log_must truncate -s $MINVDEVSIZE "$TESTFILE"

# Attach two block devices to the same file (platform-specific)
if is_linux; then
	BLKDEV1=$(losetup -f --show "$TESTFILE")
	BLKDEV2=$(losetup -f --show "$TESTFILE")
elif is_freebsd; then
	BLKDEV1=/dev/$(mdconfig -a -t vnode -f "$TESTFILE")
	BLKDEV2=/dev/$(mdconfig -a -t vnode -f "$TESTFILE")
else
	log_unsupported "Platform not supported for this test"
fi

log_note "Using devices: $BLKDEV1 $BLKDEV2"

# Attempt to create a mirror pool; this should fail because both
# devices refer to the same underlying file.
log_mustnot zpool create $TESTPOOL2 mirror $BLKDEV1 $BLKDEV2

# Re-run to capture the error message for content verification
errmsg=$(zpool create $TESTPOOL2 mirror $BLKDEV1 $BLKDEV2 2>&1)
log_note "zpool create output: $errmsg"

# Error message should name one of the devices
log_must eval "echo '$errmsg' | grep -qE '$BLKDEV1|$BLKDEV2'"

# Error message should name the active pool
if echo "$errmsg" | grep -q "active pool"; then
	log_note "Error message correctly identifies the active pool"
else
	log_fail "Error message does not mention the active pool: $errmsg"
fi

log_pass "'zpool create' reports device-specific errors for in-use vdevs."
