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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zpool contract' on an exported pool produces an
# error and does not crash.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Export the pool.
# 3. Attempt 'zpool contract' on the exported pool.
# 4. Verify the command fails.
# 5. Verify the error output is meaningful.
# 6. Re-import the pool and verify it is intact.
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - importing pool if exported"
	zpool import -d $TEST_BASE_DIR $TESTPOOL 2>/dev/null
	log_note "DEBUG: cleanup - destroying pool if it exists"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_note "DEBUG: cleanup - restoring tile size tunable"
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	log_note "DEBUG: cleanup - removing vdev files"
	rm -f $TEST_BASE_DIR/vdev_file.*
}

log_assert "'zpool contract' on exported pool must fail"
log_onexit cleanup

log_note "DEBUG: creating 3 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anymirror1 pool with 3 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: exporting pool"
log_must zpool export $TESTPOOL

#
# Attempt contraction on exported pool
#
log_note "DEBUG: attempting contraction on exported pool"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

#
# Verify the error output is meaningful
#
log_note "DEBUG: verifying error message is meaningful"
errmsg=$(zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi

#
# Re-import and verify pool is intact
#
log_note "DEBUG: re-importing pool"
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

log_note "DEBUG: verifying pool is ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying disk count is still 3"
disk_count=$(zpool status $TESTPOOL | grep -c "vdev_file")
log_note "DEBUG: disk count = $disk_count"
if [[ "$disk_count" -ne 3 ]]; then
	log_fail "Disk count changed unexpectedly: expected=3 got=$disk_count"
fi

log_pass "'zpool contract' on exported pool must fail"
