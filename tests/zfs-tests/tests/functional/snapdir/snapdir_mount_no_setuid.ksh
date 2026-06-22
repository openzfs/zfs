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

log_assert "Verify setuid flag on snapshot automounts can be disabled via tunable"

if ! is_linux ; then
	log_unsupported "SNAPSHOT_NO_SETUID tunable only available on Linux"
fi

save_tunable SNAPSHOT_NO_SETUID

function cleanup
{
	destroy_pool $TESTPOOL
	restore_tunable SNAPSHOT_NO_SETUID
}

log_onexit cleanup

# Create a pool with a couple of snapshots
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap1
log_must zfs snapshot $TESTPOOL/$TESTFS@snap2

# Set the tunable to enable setuid.
log_must set_tunable64 SNAPSHOT_NO_SETUID 0

# Trigger automount on the first snapshot, verify it does not have the
# 'nosuid' option.
ls $TESTDIR/$SNAPROOT/snap1
mountline=$(mount | grep $TESTPOOL/$TESTFS@snap1)
log_must test -n "$mountline" -a "$mountline" = "${mountline/nosuid}"

# Set the tunable to disable setuid.
log_must set_tunable64 SNAPSHOT_NO_SETUID 1

# Trigger automount on the second snapshot, verify it has the 'nosuid' option.
ls $TESTDIR/$SNAPROOT/snap2
mountline=$(mount | grep $TESTPOOL/$TESTFS@snap2)
log_must test -n "$mountline" -a "$mountline" != "${mountline/nosuid}"

log_pass "Snapshot automounts do not recieve setuid flag when disabled"
