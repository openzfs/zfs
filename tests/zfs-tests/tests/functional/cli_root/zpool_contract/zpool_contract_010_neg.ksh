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
# Verify that 'zpool contract' with a mismatched vdev type name
# produces an error. For example, specifying an anymirror name
# when the pool has anyraidz, or vice versa. Mirrors
# zpool_create_anyraid_010_neg which tests mixing types.
#
# STRATEGY:
# 1. Create an anyraidz1:2 pool with 4 disks.
# 2. Attempt 'zpool contract' using an anymirror vdev name.
# 3. Verify the command fails.
# 4. Create an anymirror1 pool with 3 disks.
# 5. Attempt 'zpool contract' using an anyraidz vdev name.
# 6. Verify the command fails.
# 7. Verify both pools are still intact.
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - destroying pools if they exist"
	poolexists ${TESTPOOL}_raidz && destroy_pool ${TESTPOOL}_raidz
	poolexists ${TESTPOOL}_mirror && destroy_pool ${TESTPOOL}_mirror
	log_note "DEBUG: cleanup - restoring tile size tunable"
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	log_note "DEBUG: cleanup - removing vdev files"
	rm -f $TEST_BASE_DIR/vdev_file.*
}

log_assert "'zpool contract' with mismatched vdev type name must fail"
log_onexit cleanup

log_note "DEBUG: creating 7 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5,6}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Test 1: anyraidz pool with anymirror vdev name
#
log_note "DEBUG: creating anyraidz1:2 pool with 4 disks"
log_must create_pool ${TESTPOOL}_raidz anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

log_note "DEBUG: attempting contraction with wrong vdev type (anymirror1-0 on raidz pool)"
log_mustnot zpool contract ${TESTPOOL}_raidz anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3

log_note "DEBUG: verifying raidz pool is still ONLINE"
log_must check_pool_status ${TESTPOOL}_raidz state ONLINE true

#
# Test 2: anymirror pool with anyraidz vdev name
#
log_note "DEBUG: creating anymirror1 pool with 3 disks"
log_must create_pool ${TESTPOOL}_mirror anymirror1 \
	$TEST_BASE_DIR/vdev_file.{4,5,6}

log_note "DEBUG: attempting contraction with wrong vdev type (anyraidz1-0 on mirror pool)"
log_mustnot zpool contract ${TESTPOOL}_mirror anyraidz1-0 \
	$TEST_BASE_DIR/vdev_file.6

log_note "DEBUG: verifying mirror pool is still ONLINE"
log_must check_pool_status ${TESTPOOL}_mirror state ONLINE true

log_pass "'zpool contract' with mismatched vdev type name must fail"
