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

log_assert "Verify that idle snapshot automounts will be expired."

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

# Create a pool and a snapshot
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Trigger the mount
log_must ls -l $TESTDIR/fs/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/fs/$SNAPROOT/snap"

# Wait for the expiry, then check it has been unmounted
log_note "sleeping until snapshot expiry time has passed"
sleep 4
log_must test -z "$(get_mount_paths $TESTPOOL/$TESTFS@snap)"

log_pass "Idle snapshot automounts are expired."
