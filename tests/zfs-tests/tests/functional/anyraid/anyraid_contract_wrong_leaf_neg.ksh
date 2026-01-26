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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that specifying a leaf vdev that is not a child of the
# anyraid vdev fails with ENXIO.
#
# STRATEGY:
# 1. Create pool with anymirror1 vdev (3 disks) + traditional mirror (2 disks)
# 2. Attempt contraction specifying a disk from the mirror vdev
# 3. Verify command fails
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contraction with wrong leaf vdev is rejected"

log_note "DEBUG: creating pool with anymirror1 + traditional mirror"
log_must zpool create -f $TESTPOOL \
	anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2} \
	mirror $TEST_BASE_DIR/vdev_file.{3,4}

log_note "DEBUG: pool status"
zpool status $TESTPOOL

log_note "DEBUG: attempting contraction with disk from mirror vdev"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3
log_note "DEBUG: contraction correctly rejected (wrong leaf)"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "Contraction with wrong leaf vdev is rejected"
