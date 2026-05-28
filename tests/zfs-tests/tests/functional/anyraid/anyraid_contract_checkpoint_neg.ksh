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
# Verify that contraction is rejected when a pool checkpoint exists
# (EBUSY). After discarding the checkpoint, contraction should succeed.
#
# STRATEGY:
# 1. Create anymirror1 pool with 4 disks
# 2. Take a checkpoint
# 3. Attempt contraction, verify failure
# 4. Discard checkpoint
# 5. Verify contraction now succeeds
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contraction is rejected while a checkpoint exists"

log_note "DEBUG: creating pool with 4 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile
typeset cksum=$(xxh128digest /$TESTPOOL/testfile)
log_note "DEBUG: testfile checksum=$cksum"

log_note "DEBUG: taking checkpoint"
log_must zpool checkpoint $TESTPOOL

log_note "DEBUG: attempting contraction while checkpoint exists"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3
log_note "DEBUG: contraction correctly rejected (checkpoint exists)"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying data intact"
newcksum=$(xxh128digest /$TESTPOOL/testfile)
[[ "$newcksum" == "$cksum" ]] || \
	log_fail "Checksum mismatch after rejected contraction"

log_note "DEBUG: discarding checkpoint"
log_must zpool checkpoint -wd $TESTPOOL

log_note "DEBUG: attempting contraction after checkpoint discarded"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3

log_note "DEBUG: waiting for relocation to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: verifying data intact after successful contraction"
newcksum=$(xxh128digest /$TESTPOOL/testfile)
[[ "$newcksum" == "$cksum" ]] || \
	log_fail "Checksum mismatch after contraction"

log_note "DEBUG: verifying pool ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "Contraction is rejected while a checkpoint exists"
