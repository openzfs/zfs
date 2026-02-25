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
# Verify that writing more data than an anymirror1 pool with small
# mixed-size disks can hold results in ENOSPC. This is the inverse
# of test 006 -- it confirms the pool correctly enforces capacity
# limits with heterogeneous disk sizes.
#
# STRATEGY:
# 1. Create anymirror1 pool with 3 small mixed-size disks (512M, 768M, 1G).
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Attempt to write a 600MiB file (exceeds usable capacity).
# 4. Verify the write fails (ENOSPC).
# 5. Verify pool is still ONLINE and healthy after the failure.
# 6. Verify any partially written data is still readable (no corruption).
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.0
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.1
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.2

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Writing 600MiB to a small mixed-size anymirror1 pool must fail with ENOSPC"

log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.0 \
	$TEST_BASE_DIR/vdev_file.1 \
	$TEST_BASE_DIR/vdev_file.2

#
# Attempt to write 600MiB. This must fail because the pool's usable
# capacity (~512MiB with mirroring) is less than 600MiB.
#
file_write -o create -b 1048576 -c 600 -d 'R' \
	-f /$TESTPOOL/oversized_file.0
write_rc=$?

if (( write_rc == 0 )); then
	log_fail "600MiB write succeeded but should have failed with ENOSPC"
fi

#
# Verify the pool is still healthy after the ENOSPC.
#
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Run scrub to verify no corruption from the partial write.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "checksum errors detected after scrub"

log_pass "Writing 600MiB to a small mixed-size anymirror1 pool must fail with ENOSPC"
