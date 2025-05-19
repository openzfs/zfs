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
# Copyright (c) 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Anyraid works correctly with checkpoints
#
# STRATEGY:
# 1. Create an anyraid vdev
# 2. Take a checkpoint
# 3. Allocate more space
# 4. Roll back to the checkpoint
# 5. Verify that the tile map looks like what it did originally
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
}

log_onexit cleanup

log_must create_pool $TESTPOOL anyraid1 $DISKS

log_assert "Anyraid works correctly with checkpoints"

map=$(zdb --anyraid-map $TESTPOOL)
log_must zpool checkpoint $TESTPOOL

log_must dd if=/dev/urandom of=/$TESTPOOL/f1 bs=1M count=2k

log_must zpool export $TESTPOOL
log_must zpool import --rewind-to-checkpoint $TESTPOOL
map2=$(zdb --anyraid-map $TESTPOOL)
log_must test "$map" == "$map2"

log_pass "Anyraid works correctly with checkpoints"
