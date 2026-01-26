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
# Verify that 'zpool contract' with invalid option flags produces
# an error and does not crash. Mirrors zpool_create_anyraid_005_neg
# which tests invalid parity strings.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Attempt 'zpool contract' with various invalid flags (-f, -n, -x).
# 3. Verify each attempt fails.
# 4. Verify pool is still intact.
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

log_assert "'zpool contract' with invalid option flags must fail"
log_onexit cleanup

log_note "DEBUG: creating 3 vdev files (768M each)"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: setting tile size tunable to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_note "DEBUG: creating anymirror1 pool with 3 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Try various invalid flags
#
log_note "DEBUG: testing 'zpool contract -f' (invalid flag)"
log_mustnot zpool contract -f $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

log_note "DEBUG: testing 'zpool contract -n' (invalid flag)"
log_mustnot zpool contract -n $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

log_note "DEBUG: testing 'zpool contract -x' (invalid flag)"
log_mustnot zpool contract -x $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

log_note "DEBUG: testing 'zpool contract --force' (invalid long option)"
log_mustnot zpool contract --force $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2

#
# Verify pool is still intact
#
log_note "DEBUG: verifying pool still exists and is ONLINE"
log_must poolexists $TESTPOOL
log_must check_pool_status $TESTPOOL state ONLINE true

log_note "DEBUG: verifying disk count unchanged (still 3)"
disk_count=$(zpool status $TESTPOOL | grep -c "vdev_file")
log_note "DEBUG: disk count = $disk_count"
if [[ "$disk_count" -ne 3 ]]; then
	log_fail "Disk count changed unexpectedly: expected=3 got=$disk_count"
fi

log_pass "'zpool contract' with invalid option flags must fail"
