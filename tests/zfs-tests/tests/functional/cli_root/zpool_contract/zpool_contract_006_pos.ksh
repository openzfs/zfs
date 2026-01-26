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
# Verify that 'zpool contract' works on an anyraidz1:2 pool.
# This is the CLI-level raidz contraction test, mirroring
# zpool_create_anyraid_001_pos which tests basic creation.
#
# STRATEGY:
# 1. Create an anyraidz1:2 pool with 5 disks.
# 2. Write data and record checksums.
# 3. Look up the anyraidz vdev name from zpool status.
# 4. Run 'zpool contract' to remove one disk.
# 5. Wait for contraction to complete.
# 6. Verify disk count decreased.
# 7. Verify data integrity.
# 8. Verify pool is ONLINE.
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

log_assert "'zpool contract' works on anyraidz1:2 pool"
log_onexit cleanup

log_note "DEBUG: creating 5 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anyraidz1:2 pool with 5 disks"
log_must create_pool $TESTPOOL anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_note "DEBUG: looking up anyraidz vdev name"
vdev_name=$(zpool status $TESTPOOL | grep "anyraidz" | awk '{print $1}')
log_note "DEBUG: vdev name = $vdev_name"

log_note "DEBUG: counting initial child vdevs"
initial_count=$(zpool status $TESTPOOL | grep -c "vdev_file")
log_note "DEBUG: initial disk count = $initial_count"

log_note "DEBUG: writing test data"
log_must dd if=/dev/urandom of=/$TESTPOOL/testfile bs=1M count=32
checksum=$(xxh128sum /$TESTPOOL/testfile)
log_note "DEBUG: checksum = $checksum"

log_note "DEBUG: running zpool contract to remove vdev_file.4"
log_must zpool contract $TESTPOOL $vdev_name \
	$TEST_BASE_DIR/vdev_file.4

log_note "DEBUG: waiting for contraction to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: counting child vdevs after contraction"
final_count=$(zpool status $TESTPOOL | grep -c "vdev_file")
log_note "DEBUG: final disk count = $final_count"

if [[ "$final_count" -ge "$initial_count" ]]; then
	log_fail "Disk count did not decrease: before=$initial_count after=$final_count"
fi

log_note "DEBUG: verifying data integrity"
checksum_after=$(xxh128sum /$TESTPOOL/testfile)
if [[ "$checksum" != "$checksum_after" ]]; then
	log_fail "Checksum mismatch: expected=$checksum got=$checksum_after"
fi

log_note "DEBUG: verifying pool is ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: running scrub"
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "'zpool contract' works on anyraidz1:2 pool"
