#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
errmsg=$(zpool create $TESTPOOL2 mirror $BLKDEV1 $BLKDEV2 2>&1)
ret=$?

log_note "zpool create returned $ret: $errmsg"

# Must have failed
if (( ret == 0 )); then
	log_fail "zpool create should have failed but succeeded"
fi

# Error message should name one of the devices
if echo "$errmsg" | grep -qE "$BLKDEV1|$BLKDEV2"; then
	log_note "Error message correctly identifies the device"
else
	log_fail "Error message does not identify the device: $errmsg"
fi

# Error message should name the active pool
if echo "$errmsg" | grep -q "active pool"; then
	log_note "Error message correctly identifies the active pool"
else
	log_fail "Error message does not mention the active pool: $errmsg"
fi

log_pass "'zpool create' reports device-specific errors for in-use vdevs."
