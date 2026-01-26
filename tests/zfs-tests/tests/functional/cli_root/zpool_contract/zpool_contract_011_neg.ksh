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
# Verify that 'zpool contract' on a traditional (non-anyraid) pool
# produces an error and does not crash. Contraction is only valid
# for anyraid vdevs. Mirrors zpool_create_anyraid_011_neg which
# tests single disk with non-zero parity.
#
# STRATEGY:
# 1. Create a traditional mirror pool with 3 disks.
# 2. Attempt 'zpool contract' using the mirror vdev name.
# 3. Verify the command fails.
# 4. Create a traditional raidz pool with 3 disks.
# 5. Attempt 'zpool contract' using the raidz vdev name.
# 6. Verify the command fails.
# 7. Verify both pools are still intact.
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - destroying pools if they exist"
	poolexists ${TESTPOOL}_mirror && destroy_pool ${TESTPOOL}_mirror
	poolexists ${TESTPOOL}_raidz && destroy_pool ${TESTPOOL}_raidz
	log_note "DEBUG: cleanup - removing vdev files"
	rm -f $TEST_BASE_DIR/vdev_file.*
}

log_assert "'zpool contract' on traditional (non-anyraid) pool must fail"
log_onexit cleanup

log_note "DEBUG: creating 6 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Test 1: traditional mirror pool
#
log_note "DEBUG: creating traditional mirror pool with 3 disks"
log_must zpool create ${TESTPOOL}_mirror mirror \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: looking up mirror vdev name"
mirror_vdev=$(zpool status ${TESTPOOL}_mirror | grep "mirror-" | awk '{print $1}')
log_note "DEBUG: mirror vdev name = $mirror_vdev"

log_note "DEBUG: attempting contraction on traditional mirror"
log_mustnot zpool contract ${TESTPOOL}_mirror $mirror_vdev \
	$TEST_BASE_DIR/vdev_file.2

log_note "DEBUG: verifying error message is meaningful"
errmsg=$(zpool contract ${TESTPOOL}_mirror $mirror_vdev \
	$TEST_BASE_DIR/vdev_file.2 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi

log_note "DEBUG: verifying mirror pool is still ONLINE"
log_must check_pool_status ${TESTPOOL}_mirror state ONLINE true

#
# Test 2: traditional raidz pool
#
log_note "DEBUG: creating traditional raidz pool with 3 disks"
log_must zpool create ${TESTPOOL}_raidz raidz \
	$TEST_BASE_DIR/vdev_file.{3,4,5}

log_note "DEBUG: looking up raidz vdev name"
raidz_vdev=$(zpool status ${TESTPOOL}_raidz | grep "raidz1-" | awk '{print $1}')
log_note "DEBUG: raidz vdev name = $raidz_vdev"

log_note "DEBUG: attempting contraction on traditional raidz"
log_mustnot zpool contract ${TESTPOOL}_raidz $raidz_vdev \
	$TEST_BASE_DIR/vdev_file.5

log_note "DEBUG: verifying raidz pool is still ONLINE"
log_must check_pool_status ${TESTPOOL}_raidz state ONLINE true

log_pass "'zpool contract' on traditional (non-anyraid) pool must fail"
