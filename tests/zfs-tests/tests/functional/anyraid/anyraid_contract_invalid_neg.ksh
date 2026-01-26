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
# Verify that attempting to contract a non-anyraid vdev produces
# a clear error (EINVAL).
#
# STRATEGY:
# 1. Create a traditional mirror pool with 3 disks
# 2. Attempt zpool contract specifying the mirror vdev
# 3. Verify command fails
# 4. Verify pool is still ONLINE
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_assert "Contraction of non-anyraid vdev is rejected"

log_note "DEBUG: creating traditional mirror pool with 3 disks"
log_must create_pool $TESTPOOL mirror \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: writing test data"
log_must file_write -o create -b 1048576 -c 1 -d 'R' \
	-f /$TESTPOOL/testfile

log_note "DEBUG: attempting contraction on traditional mirror vdev"
log_mustnot zpool contract $TESTPOOL mirror-0 \
	$TEST_BASE_DIR/vdev_file.2
log_note "DEBUG: contraction correctly rejected for non-anyraid vdev"

log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "Contraction of non-anyraid vdev is rejected"
