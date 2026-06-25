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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

verify_runnable "both"

log_assert "Verify ADMIN_SNAPSHOT tunable prevents snapdir ops from modify snapshots."

if ! is_linux ; then
	log_unsupported "ADMIN_SNAPSHOT tunable not available on this platform."
fi

save_tunable ADMIN_SNAPSHOT

function cleanup
{
	destroy_pool $TESTPOOL
	restore_tunable ADMIN_SNAPSHOT
}

log_onexit cleanup

# Disable snapdir admin ops
log_must set_tunable64 ADMIN_SNAPSHOT 0

# Create a pool and a dataset
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS

# Ensure snapshot dir does not exist for non-existent snapshot
log_mustnot test -d $TESTDIR/$SNAPROOT/snap

# Ensure we can't create the snapshot with mkdir
log_mustnot mkdir $TESTDIR/$SNAPROOT/snap

# Really create it
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Snapdir now exists
log_must test -d $TESTDIR/$SNAPROOT/snap

# And we can't delete it
log_mustnot rmdir $TESTDIR/$SNAPROOT/snap

# Nor rename it
log_mustnot mv $TESTDIR/$SNAPROOT/snap $TESTDIR/$SNAPROOT/newsnap

# Can only delete it the normal way
log_must zfs destroy $TESTPOOL/$TESTFS@snap

log_pass "ADMIN_SNAPSHOT tunable prevents snapdir ops from modifying snapshots."
