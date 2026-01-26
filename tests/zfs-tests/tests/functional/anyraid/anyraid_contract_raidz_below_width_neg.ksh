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
# Verify that contracting anyraidz vdevs at minimum width is rejected.
# Tests anyraidz1:2 at 3 disks (min = 1 parity + 2 data) and
# anyraidz2:2 at 4 disks (min = 2 parity + 2 data).
#
# STRATEGY:
# 1. Create anyraidz1:2 pool with exactly 3 disks
# 2. Attempt contraction, verify failure (ENODEV)
# 3. Destroy pool
# 4. Create anyraidz2:2 pool with exactly 4 disks
# 5. Attempt contraction, verify failure (ENODEV)
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_assert "Contraction on anyraidz at minimum width is rejected"

#
# Test 1: anyraidz1:2 with 3 disks (minimum = 1 parity + 2 data)
#
log_note "DEBUG: creating sparse files for anyraidz1:2 test"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anyraidz1:2 pool with 3 disks (minimum)"
log_must create_pool $TESTPOOL anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile
typeset cksum=$(xxh128digest /$TESTPOOL/testfile)
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
	log_fail "Checksum mismatch after rejected contraction (raidz1)"

log_note "DEBUG: destroying anyraidz1:2 pool"
log_must destroy_pool $TESTPOOL
rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Test 2: anyraidz2:2 with 4 disks (minimum = 2 parity + 2 data)
#
log_note "DEBUG: creating sparse files for anyraidz2:2 test"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3}

log_note "DEBUG: creating anyraidz2:2 pool with 4 disks (minimum)"
log_must create_pool $TESTPOOL anyraidz2:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile
cksum=$(xxh128digest /$TESTPOOL/testfile)
log_note "DEBUG: testfile checksum=$cksum"

log_note "DEBUG: looking up anyraidz vdev name"
vdev_name=$(zpool status $TESTPOOL | awk '/anyraidz/ && /raidz/ {print $1; exit}')
log_note "DEBUG: vdev name=$vdev_name"
[[ -n "$vdev_name" ]] || log_fail "Could not find anyraidz vdev name"

log_note "DEBUG: attempting contraction on minimum-width anyraidz2:2 pool"
log_mustnot zpool contract $TESTPOOL $vdev_name \
	$TEST_BASE_DIR/vdev_file.3
log_note "DEBUG: contraction correctly rejected for anyraidz2:2"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying data intact"
newcksum=$(xxh128digest /$TESTPOOL/testfile)
[[ "$newcksum" == "$cksum" ]] || \
	log_fail "Checksum mismatch after rejected contraction (raidz2)"

log_pass "Contraction on anyraidz at minimum width is rejected"
