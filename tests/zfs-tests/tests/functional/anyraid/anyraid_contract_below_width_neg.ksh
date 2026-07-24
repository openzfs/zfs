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
# Verify that contracting an anyraid vdev when child count equals
# logical width is rejected (ENODEV). Tests both anymirror1 at
# minimum (2 disks) and anyraidz1:2 at minimum (3 disks).
#
# STRATEGY:
# 1. Create anymirror1 pool with exactly 2 disks (minimum for parity 1)
# 2. Attempt contraction, verify failure
# 3. Verify pool is still ONLINE and data intact
# 4. Destroy pool
# 5. Create anyraidz1:2 pool with exactly 3 disks (minimum)
# 6. Attempt contraction, verify failure
# 7. Verify pool is still ONLINE and data intact
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_assert "Contraction at minimum width is rejected"

#
# Test 1: anymirror1 with 2 disks (minimum for parity 1)
#
log_note "DEBUG: creating sparse files for anymirror1 test"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anymirror1 pool with 2 disks (minimum)"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile
typeset cksum=$(xxh128digest /$TESTPOOL/testfile)
log_note "DEBUG: testfile checksum=$cksum"

log_note "DEBUG: attempting contraction on minimum-width anymirror1 pool"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.1
log_note "DEBUG: contraction correctly rejected for anymirror1"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying data intact"
newcksum=$(xxh128digest /$TESTPOOL/testfile)
[[ "$newcksum" == "$cksum" ]] || \
	log_fail "Checksum mismatch after rejected contraction"

log_note "DEBUG: destroying anymirror1 pool"
log_must destroy_pool $TESTPOOL
rm -f $TEST_BASE_DIR/vdev_file.{0,1}

#
# Test 2: anyraidz1:2 with 3 disks (minimum = 1 parity + 2 data)
#
log_note "DEBUG: creating sparse files for anyraidz1:2 test"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: creating anyraidz1:2 pool with 3 disks (minimum)"
log_must create_pool $TESTPOOL anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile
cksum=$(xxh128digest /$TESTPOOL/testfile)
log_note "DEBUG: testfile checksum=$cksum"

log_note "DEBUG: looking up anyraidz vdev name"
vdev_name=$(zpool status $TESTPOOL | awk '/anyraidz/ && /raidz/ {print $1; exit}')
log_note "DEBUG: vdev name=$vdev_name"
[[ -n "$vdev_name" ]] || log_fail "Could not find anyraidz vdev name"

log_note "DEBUG: attempting contraction on minimum-width anyraidz1:2 pool"
log_mustnot zpool contract $TESTPOOL $vdev_name \
	$TEST_BASE_DIR/vdev_file.2
log_note "DEBUG: contraction correctly rejected for anyraidz1:2"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying data intact"
newcksum=$(xxh128digest /$TESTPOOL/testfile)
[[ "$newcksum" == "$cksum" ]] || \
	log_fail "Checksum mismatch after rejected contraction"

log_pass "Contraction at minimum width is rejected"
