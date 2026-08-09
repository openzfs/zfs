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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

#
# DESCRIPTION:
#	Verify renamed snapshots via mv can be destroyed
#
# STRATEGY:
#	1. Create snapshot
#	2. Rename the snapshot via mv command
#	2. Verify destroying the renamed snapshot via 'zfs destroy' succeeds
#

verify_runnable "both"

if ! is_linux ; then
	log_unsupported "ADMIN_SNAPSHOT tunable not available on this platform."
fi

save_tunable ADMIN_SNAPSHOT

function cleanup
{
	datasetexists $SNAPFS && destroy_dataset $SNAPFS -Rf
	datasetexists $TESTPOOL/$TESTFS@snap_a && destroy_dataset $TESTPOOL/$TESTFS@snap_a -Rf
	datasetexists $TESTPOOL/$TESTFS@snap_b && destroy_dataset $TESTPOOL/$TESTFS@snap_b -Rf
	datasetexists $TESTPOOL/$TESTCLONE@snap_a && destroy_dataset $TESTPOOL/$TESTCLONE@snap_a -Rf
	datasetexists $TESTPOOL/$TESTCLONE && destroy_dataset $TESTPOOL/$TESTCLONE
	datasetexists $TESTPOOL/$TESTFS && destroy_dataset $TESTPOOL/$TESTFS

	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	restore_tunable ADMIN_SNAPSHOT
}

log_assert "Verify renamed snapshots via mv can be destroyed."
log_onexit cleanup

log_must set_tunable64 ADMIN_SNAPSHOT 1

# scenario 1

log_must zfs snapshot $SNAPFS
log_must mv $TESTDIR/$SNAPROOT/$TESTSNAP $TESTDIR/$SNAPROOT/snap_a

datasetexists $TESTPOOL/$TESTFS@snap_a || \
	log_fail "rename snapshot via mv in .zfs/snapshot fails."
log_must zfs destroy $TESTPOOL/$TESTFS@snap_a

# scenario 2

log_must zfs snapshot $SNAPFS
log_must zfs clone $SNAPFS $TESTPOOL/$TESTCLONE
log_must mv $TESTDIR/$SNAPROOT/$TESTSNAP $TESTDIR/$SNAPROOT/snap_b

datasetexists $TESTPOOL/$TESTFS@snap_b || \
        log_fail "rename snapshot via mv in .zfs/snapshot fails."
log_must zfs promote $TESTPOOL/$TESTCLONE
# promote back to $TESTPOOL/$TESTFS for scenario 3
log_must zfs promote $TESTPOOL/$TESTFS
log_must zfs destroy $TESTPOOL/$TESTCLONE
log_must zfs destroy $TESTPOOL/$TESTFS@snap_b

# scenario 3

log_must zfs snapshot $SNAPFS
log_must zfs clone $SNAPFS $TESTPOOL/$TESTCLONE
log_must zfs rename $SNAPFS $TESTPOOL/$TESTFS@snap_a
log_must zfs promote $TESTPOOL/$TESTCLONE
log_must zfs destroy $TESTPOOL/$TESTFS
log_must zfs destroy $TESTPOOL/$TESTCLONE@snap_a

log_pass "Verify renamed snapshots via mv can be destroyed."
