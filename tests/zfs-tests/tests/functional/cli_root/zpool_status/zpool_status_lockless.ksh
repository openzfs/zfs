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
# Verify 'zpool status --lockless|--trylock' work correctly

DISK=$TEST_BASE_DIR/zpool_status_lockless_vdev

function cleanup
{
	log_must restore_tunable ALLOW_LOCKLESS_ZPOOL_STATUS
	log_must restore_tunable DEBUG_NAMESPACE_LOCK_DELAY
	log_must restore_tunable SPA_NAMESPACE_TRYLOCK_MS
	log_must zpool destroy testpool2
	log_must rm -f "$DISK"
}

log_assert "Verify 'zpool status ---lockless|--trylock'"

log_onexit cleanup

log_must save_tunable ALLOW_LOCKLESS_ZPOOL_STATUS
log_must save_tunable DEBUG_NAMESPACE_LOCK_DELAY
log_must save_tunable SPA_NAMESPACE_TRYLOCK_MS

log_mustnot test -e $DISK
log_must truncate -s 100M $DISK
log_must zpool create testpool2 $DISK
log_must zpool status
log_must zpool status --trylock

# This should fail since zfs_allow_lockless_zpool_status!=1
log_mustnot zpool status --lockless

# Add an artificial 1s delay after taking the spa_namespace lock
set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 500
zpool status &
sleep 0.1

# This should fail since the previously spawned off 'zpool status' is holding
# the lock for at least 500ms, and --trylock only tries for 100ms.
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 100
log_mustnot zpool status --trylock
wait

set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 100
zpool status &
sleep 0.05

# This should succeed since the previously spawned off 'zpool status' is holding
# the lock for only 100ms, and --trylock tries for 300ms.
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 300
log_must zpool status --trylock
wait

# Now we artificially hold the lock for 500ms, and check that --lockless
# returns in a short amount of time, and without error.
set_tunable32 DEBUG_NAMESPACE_LOCK_DELAY 500
set_tunable32 SPA_NAMESPACE_TRYLOCK_MS 10
set_tunable32 ALLOW_LOCKLESS_ZPOOL_STATUS 1
zpool status &
sleep 0.05
before=$(get_unix_timestamp_in_ms)
log_must zpool status --lockless
after=$(get_unix_timestamp_in_ms)
d=$(($after - $before))
wait
log_note "'zpool status --lockless' took $d milliseconds"
log_must test $d -le 300

log_pass "zpool status --lockless|--trylock work correctly"
