#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2025 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/large_label/large_label.kshlib


#
# DESCRIPTION: Verify that mixed mirrors can exist and work correctly
#
# STRATEGY:
#	1. Create a pool with a small device that uses the old label layout
#	2. Attach a large device that wants to use the new label
#	3. Verify correct pool operation
#	4. Split the pool, verify each sub-pool behaves correctly.
#

function cleanup {
	log_pos zpool destroy $TESTPOOL
	log_pos zpool destroy $TESTPOOL2
	log_must rm $mntpnt/*1
}

log_assert "Verify that mixed mirrors can exist and work correctly"
log_onexit cleanup

mntpnt="$TESTDIR1"
log_must truncate -s 2T $mntpnt/big1
log_must truncate -s 2G $mntpnt/small1

log_must create_pool -f $TESTPOOL $mntpnt/small1
log_must zfs create $TESTPOOL/fs
log_must file_write -o create -d 'R' -f /$TESTPOOL/fs/f1 -b 1M -c 16
log_must touch /$TESTPOOL/f2

log_must sync_pool $TESTPOOL

log_must zpool attach $TESTPOOL $mntpnt/small1 $mntpnt/big1
log_must zpool wait -t resilver $TESTPOOL
log_must uses_large_label $mntpnt/big1
log_must uses_old_label $mntpnt/small1
log_must zpool scrub -w $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $mntpnt $TESTPOOL

log_must zpool split $TESTPOOL $TESTPOOL2
log_must zpool import -d $mntpnt $TESTPOOL2
log_must uses_large_label $mntpnt/big1
log_must uses_old_label $mntpnt/small1
log_must zpool scrub -w $TESTPOOL
log_must zpool scrub -w $TESTPOOL2

log_pass "Mixed mirrors can exist and work correctly"
