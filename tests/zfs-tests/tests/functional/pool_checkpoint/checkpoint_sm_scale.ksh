#!/bin/ksh -p

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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	The maximum address that can be described by the current space
#	map design (assuming the minimum 512-byte addressable storage)
#	limits the maximum allocatable space of any top-level vdev to
#	64PB whenever a vdev-wide space map is used.
#
#	Since a vdev-wide space map is introduced for the checkpoint
#	we want to ensure that we cannot checkpoint a pool that has a
#	top-level vdev with more than 64PB of allocatable space.
#
#	Note: Since this is a pool created from file-based vdevs we
#	      are guaranteed that vdev_ashift  is SPA_MINBLOCKSHIFT
#	      [which is currently 9 and (1 << 9) = 512], so the numbers
#	      work out for this test.
#
# STRATEGY:
#	1. Create pool with a disk of exactly 64PB
#	   (so ~63.5PB of allocatable space)
#	2. Ensure that you can checkpoint it
#	3. Create pool with a disk of exactly 65PB
#	   (so ~64.5PB of allocatable space)
#	4. Ensure we fail trying to checkpoint it
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

log_must zpool create $TESTPOOL1 $DISK64PB
log_must zpool create $TESTPOOL2 $DISK65PB

log_must zpool checkpoint $TESTPOOL1
log_mustnot zpool checkpoint $TESTPOOL2

log_pass "Attempting to checkpoint a pool with a vdev that's more than 64PB."
