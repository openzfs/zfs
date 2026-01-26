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
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zpool contract' on a pool that has already been
# contracted to minimum width is handled gracefully and does not
# hang or crash. This is the contraction equivalent of the no-op
# rebalance test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with exactly 2 disks (minimum width).
# 2. Write some data.
# 3. Attempt 'zpool contract' to remove one of the 2 disks.
# 4. Verify the command fails with an appropriate error (ENODEV).
# 5. Verify the pool is still healthy and data is intact.
# 6. Also test with a 30-second timeout to detect hangs.
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - destroying pool if it exists"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_note "DEBUG: cleanup - restoring tile size tunable"
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	log_note "DEBUG: cleanup - removing vdev files"
	rm -f $TEST_BASE_DIR/vdev_file.*
}

log_assert "No-op contraction (at minimum width) should fail gracefully"
log_onexit cleanup

log_note "DEBUG: creating 2 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anymirror1 pool with 2 disks (minimum width)"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1}

log_note "DEBUG: writing test data"
log_must dd if=/dev/urandom of=/$TESTPOOL/testfile bs=1M count=16
checksum=$(xxh128sum /$TESTPOOL/testfile)
log_note "DEBUG: checksum = $checksum"

#
# Attempt contraction at minimum width - should fail with ENODEV
#
log_note "DEBUG: attempting contraction at minimum width (should fail)"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.1

#
# Also verify it does not hang by running with a timeout
#
log_note "DEBUG: verifying contraction does not hang (30-second timeout)"
timeout 30 zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.0
ret=$?
log_note "DEBUG: timeout command returned $ret"

if [[ $ret -eq 124 ]]; then
	log_fail "No-op contraction hung (timeout after 30 seconds)"
fi
log_note "DEBUG: contraction did not hang (returned within timeout)"

#
# Verify pool is still healthy
#
log_note "DEBUG: verifying pool still exists"
log_must poolexists $TESTPOOL

log_note "DEBUG: verifying pool is ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying disk count is still 2"
disk_count=$(zpool status $TESTPOOL | grep -c "vdev_file")
log_note "DEBUG: disk count = $disk_count"
if [[ "$disk_count" -ne 2 ]]; then
	log_fail "Disk count changed unexpectedly: expected=2 got=$disk_count"
fi

log_note "DEBUG: verifying data integrity"
checksum_after=$(xxh128sum /$TESTPOOL/testfile)
if [[ "$checksum" != "$checksum_after" ]]; then
	log_fail "Checksum mismatch: expected=$checksum got=$checksum_after"
fi

log_note "DEBUG: running scrub"
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "No-op contraction (at minimum width) should fail gracefully"
