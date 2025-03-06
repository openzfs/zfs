#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.

# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#       refquota limits the amount of space a dataset can consume,
#       This test verifies that zfs receive does not override
#       refquota.
#
# STRATEGY:
#       1. Create a sub-filesystem $TESTSUBFS1
#       2. Create a file in the sub-filesystem $TESTSUBFS1
#       3. Create a snapshot of the sub-filesystem $TESTSUBFS1
#       4. Create another sub-filesystem $TESTSUBFS2
#       5. Apply a refquota value to $TESTSUBFS2,
#		half the sub-filesystem $TESTSUBFS1 file size
#       6. Verify that zfs receive of the snapshot of $TESTSUBFS1
#		fails due to refquota
#

verify_runnable "both"

oldvalue=$(get_tunable SPA_ASIZE_INFLATION)
function cleanup
{
	set_tunable32 SPA_ASIZE_INFLATION $oldvalue
        log_must zfs destroy -rf $TESTPOOL/$TESTFS
        log_must zfs create $TESTPOOL/$TESTFS
        log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_onexit cleanup

set_tunable32 SPA_ASIZE_INFLATION 2

TESTFILE='testfile'
FS=$TESTPOOL/$TESTFS
log_must zfs create $FS/$TESTSUBFS1
log_must zfs create $FS/$TESTSUBFS2

mntpnt1=$(get_prop mountpoint $FS/$TESTSUBFS1)
mntpnt2=$(get_prop mountpoint $FS/$TESTSUBFS2)

log_must mkfile 200M $mntpnt1/$TESTFILE
log_must zfs snapshot $FS/$TESTSUBFS1@snap200m

log_must zfs set refquota=10M $FS/$TESTSUBFS2
log_mustnot eval "zfs send $FS/$TESTSUBFS1@snap200m |" \
        "zfs receive -F $FS/$TESTSUBFS2"

log_pass "ZFS receive does not override refquota"

