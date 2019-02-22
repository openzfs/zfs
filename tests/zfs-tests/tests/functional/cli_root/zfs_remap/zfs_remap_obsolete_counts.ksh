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
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# 'zfs remap' depends on 'feature@obsolete_counts' being active
#
# STRATEGY:
# 1. Prepare a pool where a top-level VDEV has been removed and with
#    feature@obsolete_counts disabled
# 2. Verify any 'zfs remap' command cannot be executed
# 3. Verify the same commands complete successfully when
#    feature@obsolete_counts is enabled
#

# N.B. The 'zfs remap' command has been disabled and may be removed.
export ZFS_REMAP_ENABLED=YES

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $DISK1 $DISK2
}

log_assert "'zfs remap' depends on feature@obsolete_counts being active"
log_onexit cleanup

f="$TESTPOOL/fs"
v="$TESTPOOL/vol"
s="$TESTPOOL/fs@snap"
c="$TESTPOOL/clone"

DISK1="$TEST_BASE_DIR/zfs_remap-1"
DISK2="$TEST_BASE_DIR/zfs_remap-2"

# 1. Prepare a pool where a top-level VDEV has been removed with
#    feature@obsolete_counts disabled
log_must truncate -s $(($MINVDEVSIZE * 2)) $DISK1
log_must zpool create -o feature@obsolete_counts=disabled $TESTPOOL $DISK1
log_must zfs create $f
log_must zfs create -V 1M -s $v
log_must zfs snap $s
log_must zfs clone $s $c
log_must truncate -s $(($MINVDEVSIZE * 2)) $DISK2
log_must zpool add $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK1
log_must wait_for_removal $TESTPOOL

# 2. Verify any 'zfs remap' command cannot be executed
log_mustnot zfs remap $f
log_mustnot zfs remap $v
log_mustnot zfs remap $c

# 3. Verify the same commands complete successfully when
#    feature@obsolete_counts is enabled
log_must zpool set feature@obsolete_counts=enabled $TESTPOOL
log_must zfs remap $f
log_must zfs remap $v
log_must zfs remap $c

log_pass "'zfs remap' correctly depends on feature@obsolete_counts being active"
