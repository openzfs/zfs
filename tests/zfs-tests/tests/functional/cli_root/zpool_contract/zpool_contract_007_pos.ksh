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
# Verify that 'zpool contract' works on a pool with multiple anyraid
# vdev groups. Mirrors zpool_create_anyraid_004_pos which tests
# creating pools with multiple anyraid vdevs.
#
# STRATEGY:
# 1. Create a pool with two anymirror1 vdev groups.
# 2. Write data and record checksums.
# 3. Contract one disk from the first vdev group.
# 4. Verify contraction completes and data is intact.
# 5. Contract one disk from the second vdev group.
# 6. Verify contraction completes and data is intact.
# 7. Verify pool is ONLINE.
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

log_assert "'zpool contract' works on pool with multiple anyraid vdev groups"
log_onexit cleanup

log_note "DEBUG: creating 6 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating pool with two anymirror1 vdev groups (3 disks each)"
log_must create_pool $TESTPOOL \
	anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2} \
	anymirror1 $TEST_BASE_DIR/vdev_file.{3,4,5}

log_note "DEBUG: verifying two anymirror vdev groups exist"
vdev_count=$(zpool status $TESTPOOL | grep -c "anymirror")
log_note "DEBUG: anymirror vdev group count = $vdev_count"
if [[ "$vdev_count" -lt 2 ]]; then
	log_fail "Expected at least 2 anymirror vdev groups, got $vdev_count"
fi

log_note "DEBUG: writing test data"
log_must dd if=/dev/urandom of=/$TESTPOOL/testfile bs=1M count=32
checksum=$(xxh128sum /$TESTPOOL/testfile)
log_note "DEBUG: checksum = $checksum"

#
# Contract one disk from the first vdev group (anymirror1-0)
#
log_note "DEBUG: contracting vdev_file.2 from anymirror1-0"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

log_note "DEBUG: waiting for first contraction to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying data integrity after first contraction"
checksum_after1=$(xxh128sum /$TESTPOOL/testfile)
if [[ "$checksum" != "$checksum_after1" ]]; then
	log_fail "Checksum mismatch after first contraction: expected=$checksum got=$checksum_after1"
fi

#
# Contract one disk from the second vdev group (anymirror1-1)
#
log_note "DEBUG: contracting vdev_file.5 from anymirror1-1"
log_must zpool contract $TESTPOOL anymirror1-1 \
	$TEST_BASE_DIR/vdev_file.5

log_note "DEBUG: waiting for second contraction to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying data integrity after second contraction"
checksum_after2=$(xxh128sum /$TESTPOOL/testfile)
if [[ "$checksum" != "$checksum_after2" ]]; then
	log_fail "Checksum mismatch after second contraction: expected=$checksum got=$checksum_after2"
fi

log_note "DEBUG: verifying pool is ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: running scrub"
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "'zpool contract' works on pool with multiple anyraid vdev groups"
