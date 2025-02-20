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
# Copyright (c) 2025 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'ZPOOL_LOCK_BEHAVIOR=lockless|trylock|wait' works correctly with
# zpool status.

DISK=$TEST_BASE_DIR/file-vdev-lockless
ZFS_USER=zfs_user
ZFS_GROUP=zfs_group

function cleanup
{
	unset ZPOOL_LOCK_BEHAVIOR
	log_must restore_tunable DEBUG_NAMESPACE_LOCK_DELAY
	log_must restore_tunable SPA_NAMESPACE_TRYLOCK_MS
	log_must del_user $ZFS_USER
	log_must del_group $ZFS_USER
	log_must zpool destroy testpool2
	log_must rm -f "$DISK"
}

log_assert "Verify ZPOOL_LOCK_BEHAVIOR values with zpool status"

log_onexit cleanup

log_must save_tunable DEBUG_NAMESPACE_LOCK_DELAY
log_must save_tunable SPA_NAMESPACE_TRYLOCK_MS

log_must add_group $ZFS_GROUP
log_must add_user $ZFS_GROUP $ZFS_USER

log_mustnot test -e $DISK
log_must truncate -s 100M $DISK
log_must zpool create testpool2 $DISK

# Normal, safe, zpool status variants.  These should all work.
log_must user_run $ZFS_USER zpool status
export ZPOOL_LOCK_BEHAVIOR=""
log_must user_run $ZFS_USER zpool status
export ZPOOL_LOCK_BEHAVIOR=wait
log_must user_run $ZFS_USER "zpool status"
export ZPOOL_LOCK_BEHAVIOR=trylock
log_must user_run $ZFS_USER "zpool status"

# We should not have permission as an ordinary user to run lockless
export ZPOOL_LOCK_BEHAVIOR=lockless
log_mustnot user_run $ZFS_USER "zpool status"
log_must zpool status
export ZPOOL_LOCK_BEHAVIOR=""

# Add an artificial 500ms delay after taking the spa_namespace lock
set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 500
zpool status &
sleep 0.1

# This should fail since the previously spawned off 'zpool status' is holding
# the lock for at least 500ms, and "trylock" only tries for 100ms.
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 100
export ZPOOL_LOCK_BEHAVIOR=trylock
log_mustnot user_run $ZFS_USER "zpool status"
wait

set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 100
zpool status &
sleep 0.05

# This should succeed since the previously spawned off 'zpool status' is holding
# the lock for only 100ms, and "trylock" tries for 300ms.
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 300
export ZPOOL_LOCK_BEHAVIOR=trylock
log_must user_run $ZFS_USER "zpool status"
wait

# Now we artificially hold the lock for 500ms, and check that "lockless"
# returns in a shorter amount of time, and without error.
set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 500
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 10
zpool status &
sleep 0.05
before=$(get_unix_timestamp_in_ms)
export ZPOOL_LOCK_BEHAVIOR=lockless
log_must zpool status
after=$(get_unix_timestamp_in_ms)
d=$(($after - $before))
wait
log_note "'lockless' zpool status took $d milliseconds"
log_must test $d -le 300

log_pass "ZPOOL_LOCK_BEHAVIOR=lockless|trylock|wait zpool status works"
