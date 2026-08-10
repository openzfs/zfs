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
# Copyright (c) 2026 by Andrew Mochalskyi. All rights reserved.
#

. $STF_SUITE/tests/functional/snapdir/snapdir.kshlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

verify_runnable "both"

log_assert "Verify that 'zfs destroy -d' defers a snapshot held open by a mount."

if ! is_linux ; then
	log_unsupported "EXPIRE_SNAPSHOT tunable only available on Linux"
fi

save_tunable EXPIRE_SNAPSHOT

typeset -i tail_pid=0

function cleanup
{
	# Without this a failure leaves the snapshot mount busy and the pool
	# cannot be destroyed
	if ((tail_pid > 0)) ; then
		kill $tail_pid 2>/dev/null
		wait $tail_pid 2>/dev/null
	fi
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
tail_pid=$!

# Don't go on until it really has the file open, or the destroys below race
# against it and the mount is not busy when they run
typeset -i i=0
while ((i < 50)) && ! ls -l /proc/$tail_pid/fd 2>/dev/null | \
    grep -q "$SNAPROOT/snap/empty" ; do
	sleep 0.1
	((i += 1))
done
log_must test $i -lt 50

# A plain destroy has nowhere to put the request, so it fails
log_mustnot zfs destroy $TESTPOOL/$TESTFS@snap
log_must datasetexists $TESTPOOL/$TESTFS@snap

# With -d the request is recorded on the snapshot instead
log_must zfs destroy -d $TESTPOOL/$TESTFS@snap
log_must datasetexists $TESTPOOL/$TESTFS@snap
log_must test "$(get_prop defer_destroy $TESTPOOL/$TESTFS@snap)" == "on"

# Let go of the file, which releases the last reference to the mount
kill $tail_pid
wait $tail_pid
typeset -i rc=$?
tail_pid=0

# tail was still running when we signalled it, so it held the file open for
# the whole of the above; an earlier exit would show a normal status here
log_must test $rc -gt 128

# The snapshot goes away on its own once that has happened
i=0
while ((i < 30)) && datasetexists $TESTPOOL/$TESTFS@snap ; do
	sleep 1
	((i += 1))
done
log_mustnot datasetexists $TESTPOOL/$TESTFS@snap

log_pass "'zfs destroy -d' defers a snapshot held open by a mount."
