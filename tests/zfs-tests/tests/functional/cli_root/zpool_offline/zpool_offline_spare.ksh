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

# Copyright 2026 by Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that traditional spares that are active can be offlined or
#	force-faulted. Verify that in all other cases, spares cannot be
#	offlined or faulted.
#
# STRATEGY:
# 	1. Create pool with traditional spare
#	2. Verify we can't offline and fault an inactive traditional spare
#	3. Verify we can offline and fault an active traditional spare
#	4. Create draid pool with draid spare
#	5. Verify we can't offline/fault draid spare

TESTPOOL2=testpool2
function cleanup
{
	destroy_pool $TESTPOOL2
	log_must rm -f $TESTDIR/file-vdev-{1..3}
}

log_onexit cleanup
verify_runnable "global"

log_assert "Verify zpool offline has the correct behavior on spares"

# Verify any old file vdevs are gone
log_mustnot ls $TESTDIR/file-vdev-* &> /dev/null

log_must truncate -s 100M $TESTDIR/file-vdev-{1..3}

log_must zpool create $TESTPOOL2 mirror $TESTDIR/file-vdev-1 \
    $TESTDIR/file-vdev-2 spare $TESTDIR/file-vdev-3

# Test that we can't offline an inactive spare
log_mustnot zpool offline $TESTPOOL2 $TESTDIR/file-vdev-3
log_mustnot zpool offline -f $TESTPOOL2 $TESTDIR/file-vdev-3

# Test that we can offline an active spare
log_must zpool replace $TESTPOOL2 $TESTDIR/file-vdev-1 $TESTDIR/file-vdev-3
log_must zpool offline $TESTPOOL2 $TESTDIR/file-vdev-3
log_must zpool online $TESTPOOL2 $TESTDIR/file-vdev-3
log_must zpool offline -f $TESTPOOL2 $TESTDIR/file-vdev-3

destroy_pool $TESTPOOL2

log_must zpool create -f $TESTPOOL2 draid1:1d:1s:3c $TESTDIR/file-vdev-{1..3}

# Test that we can't offline an inactive draid spare
log_mustnot zpool offline $TESTPOOL2 draid1-0-0
log_mustnot zpool offline -f $TESTPOOL2 draid1-0-0

# Test that we can't offline an active draid spare
log_must zpool replace $TESTPOOL2 $TESTDIR/file-vdev-1 draid1-0-0
log_mustnot zpool offline $TESTPOOL2 draid1-0-0
log_mustnot zpool offline -f $TESTPOOL2 draid1-0-0

log_pass "zpool offline has the correct behavior on spares"
