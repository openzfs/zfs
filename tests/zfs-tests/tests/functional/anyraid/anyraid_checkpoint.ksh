#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
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

log_must create_pool $TESTPOOL anymirror1 $DISKS

log_assert "Anyraid works correctly with checkpoints"
log_must zdb --anyraid-map $TESTPOOL

map=$(zdb --anyraid-map $TESTPOOL)
log_must zpool checkpoint $TESTPOOL

log_must file_write -o create -f /$TESTPOOL/f1 -b 1048576 -c 2048 -d R

log_must zpool export $TESTPOOL
log_must zpool import --rewind-to-checkpoint $TESTPOOL
map2=$(zdb --anyraid-map $TESTPOOL)
log_must test "$map" == "$map2"

log_pass "Anyraid works correctly with checkpoints"
