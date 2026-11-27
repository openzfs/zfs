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
# Copyright (c) 2026, John White.
#

# DESCRIPTION:
# Verify async mirror frozen and off events, including duplicate handling.
#
# STRATEGY:
# 1. Turn the async mirror off and check the event and its vdev GUID.
# 2. Turn it off again and check that no duplicate event is posted.
# 3. Freeze the lagging child and check the event payload.
# 4. Export and import the frozen pool and check that import posts no event.

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

verify_runnable "global"

typeset saved_testpool=$TESTPOOL
typeset async_testpool=async_mirror_events_$$
typeset event_file=$TEST_BASE_DIR/async_mirror_events.out.$$
typeset async_mirror_dir_created=0

function cleanup
{
	zinject -c all >/dev/null 2>&1
	TESTPOOL=$async_testpool
	cleanup_async_mirror
	if (( async_mirror_dir_created )); then
		log_must rmdir "$ASYNC_MIRROR_DIR"
	fi
	TESTPOOL=$saved_testpool
	rm -f "$event_file"
}

log_assert "Async mirror off and frozen events describe state changes"
log_onexit cleanup

TESTPOOL=$async_testpool
if [[ ! -d $ASYNC_MIRROR_DIR ]]; then
	log_must mkdir -p "$ASYNC_MIRROR_DIR"
	async_mirror_dir_created=1
fi

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files ready 2
log_must wait_async_mirror_idle

log_must zpool events -c
log_must zpool set write_mode=sync "$TESTPOOL" "$ASYNC_MIRROR_LAGGING"
log_must wait_async_mirror_state off
log_must eval "zpool events -v > $event_file"
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_off' "$event_file"
log_must grep -q 'vdev_guid' "$event_file"

log_must zpool events -c
log_must zpool set write_mode=sync "$TESTPOOL" "$ASYNC_MIRROR_LAGGING"
log_must eval "zpool events -v > $event_file"
log_mustnot grep -q 'sysevent\.fs\.zfs\.async_mirror_off' "$event_file"

async_mirror_enable
async_mirror_write_files resumed 2
log_must wait_async_mirror_idle
log_must zpool events -c
async_mirror_damage_backlog "/$TESTPOOL/freeze" 1 0
log_must wait_async_mirror_frozen
log_must eval "zpool events -v > $event_file"
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_frozen' "$event_file"
log_must grep -q 'async_mirror_txg' "$event_file"
log_must grep -q 'async_mirror_errno' "$event_file"
log_must grep -q 'async_mirror_applied_txg' "$event_file"
log_must grep -q 'async_mirror_reason' "$event_file"
log_must grep -q 'vdev_guid' "$event_file"

log_must zpool events -c
log_must zpool export "$TESTPOOL"
async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must eval "zpool events -v > $event_file"
log_mustnot grep -q 'sysevent\.fs\.zfs\.async_mirror_frozen' "$event_file"

log_pass "Async mirror off and frozen events are posted once"
