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
# Verify that zpool get and zpool status report correct properties
# for AnyRAID vdevs. Check that feature@anyraid is active, that
# feature@physical_rewrite transitions from enabled to active after
# a rewrite, that zpool status output correctly shows the AnyRAID
# vdev structure, and that pool size/free/allocated are reported
# correctly. This test is self-contained and does not depend on any
# other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Verify feature@anyraid is active via zpool get.
# 3. Verify feature@physical_rewrite is enabled on fresh pool.
# 4. Verify zpool status shows anymirror1-0 vdev name.
# 5. Verify pool size, free, and allocated are reported correctly.
# 6. Write data, verify allocated increases.
# 7. Run zfs rewrite -P, verify feature@physical_rewrite becomes active.
# 8. Repeat with an anyraidz1:2 pool to verify raidz-style naming.
#

verify_runnable "global"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_assert "zpool get and zpool status report correct properties for AnyRAID"

#
# Create backing files and set tile size.
#
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2,3}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

# ---------------------------------------------------------------
# Test 1: anymirror1 pool properties
# ---------------------------------------------------------------
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Verify feature@anyraid is active.
#
typeset anyraid_feature=$(zpool get -H -o value feature@anyraid $TESTPOOL)
[[ "$anyraid_feature" == "active" ]] || \
	log_fail "feature@anyraid should be active, got: $anyraid_feature"

#
# Verify feature@physical_rewrite is enabled on a fresh pool
# (per-dataset feature, refcount=0 until a rewrite occurs).
#
typeset physrw_feature=$(zpool get -H -o value feature@physical_rewrite $TESTPOOL)
[[ "$physrw_feature" == "enabled" ]] || \
	log_fail "feature@physical_rewrite should be enabled on fresh pool, got: $physrw_feature"

#
# Verify zpool status shows the anymirror1-0 vdev name.
#
typeset status_output=$(zpool status $TESTPOOL)

echo "$status_output" | grep -q "anymirror1-0" || \
	log_fail "zpool status should show anymirror1-0 vdev"

#
# Verify all 3 disks appear in the status output.
#
echo "$status_output" | grep -q "vdev_file.0" || \
	log_fail "vdev_file.0 not found in zpool status"
echo "$status_output" | grep -q "vdev_file.1" || \
	log_fail "vdev_file.1 not found in zpool status"
echo "$status_output" | grep -q "vdev_file.2" || \
	log_fail "vdev_file.2 not found in zpool status"

#
# Verify pool health is ONLINE.
#
typeset health=$(zpool get -H -o value health $TESTPOOL)
[[ "$health" == "ONLINE" ]] || \
	log_fail "Pool health should be ONLINE, got: $health"

#
# Verify pool size is non-zero.
#
typeset pool_size=$(zpool get -H -o value -p size $TESTPOOL)
(( pool_size > 0 )) || \
	log_fail "Pool size should be > 0, got: $pool_size"

#
# Record initial allocated value, write data, verify it increases.
#
typeset alloc_before=$(zpool get -H -o value -p allocated $TESTPOOL)

log_must file_write -o create -b 1048576 -c 8 -d 'R' \
	-f /$TESTPOOL/proptest_file

log_must zpool sync $TESTPOOL

typeset alloc_after=$(zpool get -H -o value -p allocated $TESTPOOL)
(( alloc_after > alloc_before )) || \
	log_fail "Allocated should increase after write: before=$alloc_before after=$alloc_after"

#
# Verify free space is reported and is less than total size.
#
typeset pool_free=$(zpool get -H -o value -p free $TESTPOOL)
(( pool_free > 0 )) || \
	log_fail "Free space should be > 0, got: $pool_free"
(( pool_free < pool_size )) || \
	log_fail "Free space should be less than pool size"

#
# Perform a physical rewrite and verify feature@physical_rewrite
# transitions from enabled to active.
#
log_must zfs rewrite -P /$TESTPOOL/proptest_file
log_must zpool sync $TESTPOOL

physrw_feature=$(zpool get -H -o value feature@physical_rewrite $TESTPOOL)
[[ "$physrw_feature" == "active" ]] || \
	log_fail "feature@physical_rewrite should be active after rewrite, got: $physrw_feature"

log_must destroy_pool $TESTPOOL

# ---------------------------------------------------------------
# Test 2: anyraidz1:2 pool properties
# ---------------------------------------------------------------
log_must create_pool $TESTPOOL anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

#
# Verify feature@anyraid is active.
#
anyraid_feature=$(zpool get -H -o value feature@anyraid $TESTPOOL)
[[ "$anyraid_feature" == "active" ]] || \
	log_fail "feature@anyraid should be active on raidz pool, got: $anyraid_feature"

#
# Verify zpool status shows the anyraidz vdev name.
#
status_output=$(zpool status $TESTPOOL)

echo "$status_output" | grep -q "anyraidz.*-0" || \
	log_fail "zpool status should show anyraidz vdev name"

#
# Verify all 4 disks appear in the status output.
#
echo "$status_output" | grep -q "vdev_file.0" || \
	log_fail "vdev_file.0 not found in zpool status"
echo "$status_output" | grep -q "vdev_file.3" || \
	log_fail "vdev_file.3 not found in zpool status"

#
# Verify pool health is ONLINE.
#
health=$(zpool get -H -o value health $TESTPOOL)
[[ "$health" == "ONLINE" ]] || \
	log_fail "Raidz pool health should be ONLINE, got: $health"

#
# Run scrub and verify no errors on raidz pool.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "zpool get and zpool status report correct properties for AnyRAID"
