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

#
# DESCRIPTION:
#	Replacing a healthy lagging child is refused while the async mirror
#	is on.
#
# STRATEGY:
#	1. Start an async mirror.
#	2. Try to replace its healthy lagging child.
#	3. Check that the replace fails and the async mirror stays on.
#

verify_runnable "global"

function cleanup_async_mirror_replace_healthy
{
	cleanup_async_mirror
	log_must rm -f $TEST_BASE_DIR/async_mirror_replace.err
}

log_onexit cleanup_async_mirror_replace_healthy

function expect_async_mirror_active
{
	typeset vdev=$1
	typeset error_file=$TEST_BASE_DIR/async_mirror_replace.err

	if zpool replace -f $TESTPOOL $vdev $ASYNC_MIRROR_REPLACEMENT \
	    >$error_file 2>&1; then
		log_fail "replace of $vdev succeeded while the mirror was on"
	fi
	typeset expected="the pool has an active asynchronous mirror; turn "
	expected="${expected}the mode off first"
	grep -Fq "$expected" $error_file ||
	    log_fail "replace of $vdev returned the wrong error" \
	    "$(cat $error_file)"
}

async_mirror_create_pool
async_mirror_enable
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_REPLACEMENT

expect_async_mirror_active $ASYNC_MIRROR_LAGGING
expect_async_mirror_active $ASYNC_MIRROR_LEADER
typeset state=$(get_async_mirror_prop state)
[[ $state == behind || $state == current ]] ||
	log_fail "The healthy lagging child is not active"
log_must test "$(get_async_mirror_prop write_mode)" == behind
log_must test "$(get_async_mirror_stat active)" -eq 1

log_pass "A healthy lagging child cannot be replaced while the async mirror" \
    "is on."
