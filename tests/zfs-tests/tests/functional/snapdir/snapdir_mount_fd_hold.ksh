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

. $STF_SUITE/tests/functional/snapdir/snapdir.kshlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

verify_runnable "both"

log_assert "Verify that holding an fd in a snapshot mount doesn't prevent mount expiry."

if ! is_linux ; then
	log_unsupported "EXPIRE_SNAPSHOT tunable only available on Linux"
fi

save_tunable EXPIRE_SNAPSHOT

function cleanup
{
	destroy_pool $TESTPOOL
	restore_tunable EXPIRE_SNAPSHOT
}

log_onexit cleanup

# Set snapshot expiry time to something we can test sanely.
log_must set_tunable64 EXPIRE_SNAPSHOT 2

# Create a pool and a snapshot, with a file inside
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs $TESTPOOL/$TESTFS
log_must touch $TESTDIR/fs/empty
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Trigger the mount
log_must ls -l $TESTDIR/fs/$SNAPROOT/snap >/dev/null
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/fs/$SNAPROOT/snap"

# Open a file in the snapshot and hold it
tail -f $TESTDIR/fs/$SNAPROOT/snap/empty &
typeset -i tail_pid=$!

# Sleep a few seconds, until the mount expires
log_note "sleeping until snapshot expiry time has passed"
sleep 4

# Confirm mount expired
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == ""

# Try to destroy the snapshot. Should fail with EBUSY, because tail is
# holding it open.
log_mustnot zfs destroy $TESTPOOL/$TESTFS@snap

# Kill tail
kill $tail_pid
wait $tail_pid
typeset -i rc=$?

# tail should have been running after the expiry, and so our signal should
# have killed it. If it exited earlier, the return code would indicate a
# normal exit, not a signal.
log_must test $rc -gt 128

# Snapshot now fully released and destroyable
log_must zfs destroy $TESTPOOL/$TESTFS@snap

log_pass "Holding an fd in a snapshot mount doesn't prevent mount expiry."
