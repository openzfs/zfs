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

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

verify_runnable "global"

log_onexit cleanup_async_mirror

function missing_at_import_online # current|behind
{
	typeset case=$1
	typeset digests cmd history

	log_note "lagging child $case at the export"
	async_mirror_create_pool
	async_mirror_enable

	async_mirror_write_files a 2
	log_must wait_async_mirror_idle
	digests="$(async_mirror_digests a 2)"
	if [[ $case == behind ]]; then
		async_mirror_suspend_copy 1
		async_mirror_write_files b 4 1
		log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
		digests="$digests
$(async_mirror_digests b 4)"
	fi
	log_must zpool export $TESTPOOL
	[[ $case == behind ]] && async_mirror_suspend_copy 0

	async_mirror_import_leader
	log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "UNAVAIL"
	log_must test "$(get_async_mirror_prop)" == sync
	async_mirror_write_files c 2
	digests="$digests
$(async_mirror_digests c 2)"

	async_mirror_unhide_lagging
	log_must zpool online $TESTPOOL $ASYNC_MIRROR_LAGGING
	log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "ONLINE"
	log_must wait_async_mirror_resilvered
	log_must test "$(get_async_mirror_prop)" == sync

	history=$(zpool history -i $TESTPOOL)
[[ $history == *"write_mode=sync for child"* &&
    $history == *"missing or incomplete at import"* ]] ||
    log_fail "The import history does not explain the mode change"

	# Only an administrator starts the async mirror again after the resilver.
	log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
	log_must wait_async_mirror_state "behind|current"
	async_mirror_write_files d 2
	digests="$digests
$(async_mirror_digests d 2)"
	log_must wait_async_mirror_idle
	log_must test "$(get_async_mirror_prop state)" == current

	cmd="async_mirror_digests a 2"
	[[ $case == behind ]] && cmd="$cmd; async_mirror_digests b 4"
	cmd="$cmd; async_mirror_digests c 2; async_mirror_digests d 2"
	log_must test "$digests" == "$(eval "$cmd")"
	async_mirror_clear_import_errors $ASYNC_MIRROR_LAGGING
	log_must verify_pool $TESTPOOL
	async_mirror_verify_lagging_alone "$digests" eval "$cmd"
	cleanup_async_mirror
}

log_assert "An incomplete lagging child is switched to sync at import" \
    "and needs an administrator started resilver."

missing_at_import_online current
missing_at_import_online behind

log_pass "An incomplete child is resilvered before the async mirror starts again."
