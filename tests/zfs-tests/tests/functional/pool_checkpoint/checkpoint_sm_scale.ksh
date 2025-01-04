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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	The maximum address that can be described by a single-word
#	space map entry limits the maximum allocatable space of any
#	top-level vdev to 64PB whenever a vdev-wide space map is used.
#
#	Since a vdev-wide space map is introduced for the checkpoint
#	we want to ensure that we cannot checkpoint a pool that does
#	not use the new space map encoding (V2) and has a top-level
#	vdev with more than 64PB of allocatable space.
#
#	Note: Since this is a pool created from file-based vdevs we
#	      are guaranteed that vdev_ashift  is SPA_MINBLOCKSHIFT
#	      [which is currently 9 and (1 << 9) = 512], so the numbers
#	      work out for this test.
#
# STRATEGY:
#	1. Create pool with a disk of exactly 64PB
#	   (so ~63.5PB of allocatable space) and
#	   ensure that has the checkpoint feature
#	   enabled but not space map V2
#	2. Ensure that you can checkpoint it
#	3. Create pool with a disk of exactly 65PB
#	   (so ~64.5PB of allocatable space) with
#	   the same setup
#	4. Ensure we fail trying to checkpoint it
#
# Note:
# This test used to create the two pools and attempt to checkpoint
# them at the same time, then destroy them. We later had to change
# this to test one pool at a time as the metaslabs (even though empty)
# consumed a lot of memory, especially on a machine that has been
# running with debug enabled. To give an example, each metaslab
# structure is ~1712 bytes (at the time of this writing), and each
# vdev has 128K metaslabs, which means that just the structures
# consume 131071 * 1712 = ~224M.
#

verify_runnable "global"

TESTPOOL1=testpool1
TESTPOOL2=testpool2

DISK64PB=/$DISKFS/disk64PB
DISK65PB=/$DISKFS/disk65PB

function test_cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -f $DISK64PB $DISK65PB
	cleanup_test_pool
}

setup_test_pool
log_onexit test_cleanup

log_must zfs create $DISKFS
log_must mkfile -n $((64 * 1024 * 1024))g $DISK64PB
log_must mkfile -n $((65 * 1024 * 1024))g $DISK65PB

log_must zpool create -d $TESTPOOL1 $DISK64PB
log_must zpool set feature@zpool_checkpoint=enabled $TESTPOOL1
log_must zpool checkpoint $TESTPOOL1
destroy_pool $TESTPOOL1

log_must zpool create -d $TESTPOOL2 $DISK65PB
log_must zpool set feature@zpool_checkpoint=enabled $TESTPOOL2
log_mustnot zpool checkpoint $TESTPOOL2
destroy_pool $TESTPOOL2

log_pass "Fail to checkpoint pool with old spacemap encoding" \
    " and a vdev that's more than 64PB."
