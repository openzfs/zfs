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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check user|group quota to snapshot that:
#	1) can not set user|group quota to snapshot directly
#	2) snapshot can inherit the parent fs's user|groupquota
#	3) the user|group quota will not change even the parent fs's quota changed.
#
#
# STRATEGY:
#       1. create a snapshot of a fs
#       2. set the user|group quota to snapshot and expect fail
#	3. set user|group quota to fs and check the snapshot
#	4. re-set user|group quota to fs and check the snapshot's value
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the snapshot's user|group quota"
typeset snap_fs=$QFS@snap


log_must zfs set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must check_quota "userquota@$QUSER1" $QFS "$UQUOTA_SIZE"

log_must zfs set groupquota@$QGROUP=$GQUOTA_SIZE $QFS
log_must check_quota "groupquota@$QGROUP" $QFS "$GQUOTA_SIZE"

log_must zfs snapshot $snap_fs

log_note "check the snapshot $snap_fs user|group quota"
log_must check_quota "userquota@$QUSER1" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $snap_fs "$GQUOTA_SIZE"

log_note  "set userquota and groupquota to $snap_fs which will fail"
log_mustnot zfs set userquota@$QUSER1=$SNAP_QUOTA $snap_fs
log_mustnot zfs set groupquota@$QGROUP=$SNAP_QUOTA $snap_fs

log_note "change the parent's userquota and groupquota"
log_must zfs set userquota@$QUSER1=$TEST_QUOTA $QFS
log_must zfs set groupquota@$QGROUP=$TEST_QUOTA $QFS

log_must check_quota "userquota@$QUSER1" $QFS $TEST_QUOTA
log_must check_quota "groupquota@$QGROUP" $QFS $TEST_QUOTA

log_note "check the snapshot $snap_fs userquota and groupquota"
log_must check_quota "userquota@$QUSER1" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "groupquota@$QGROUP" $snap_fs "$GQUOTA_SIZE"

log_pass "Check the snapshot's user|group quota pass as expect"
