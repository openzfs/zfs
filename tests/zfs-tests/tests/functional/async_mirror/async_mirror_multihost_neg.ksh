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
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

#
# DESCRIPTION:
#	An async mirror and multihost cannot be active at the same time.
#
# STRATEGY:
#	1. Set multihost on, then verify that async mode is refused with a
#	   clear error and remains off.
#	2. Turn multihost off and enable async mode.
#	3. Verify that multihost is refused with a clear error and remains off.
#

verify_runnable "global"

typeset refusal_output="$TEST_BASE_DIR/async_mirror_multihost.$$.out"

function cleanup
{
	cleanup_async_mirror
	log_must rm -f "$refusal_output"
	log_must mmp_clear_hostid
}

function expect_refusal # error_text command ...
{
	typeset error_text=$1
	shift

	if "$@" > "$refusal_output" 2>&1; then
		log_fail "Command succeeded, expected refusal: $*"
	fi
	log_must grep -Fq "$error_text" "$refusal_output"
}

log_onexit cleanup

log_must mmp_set_hostid $HOSTID1
async_mirror_create_pool
log_must zpool set multihost=on $TESTPOOL

expect_refusal "multihost is enabled; turn it off before enabling the asynchronous mirror" \
	zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_prop state)" == off

log_must zpool set multihost=off $TESTPOOL
async_mirror_enable

expect_refusal "the pool has an active asynchronous mirror; turn the mode off first" \
	zpool set multihost=on $TESTPOOL
log_must test "$(zpool get -H -o value multihost $TESTPOOL)" == off

log_pass "An async mirror and multihost cannot be active together."
