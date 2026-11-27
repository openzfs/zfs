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
#	A mirror keeps a normal, synchronous member.
#
# STRATEGY:
#	1. Create a two-way mirror and set one child to read_mode=fallback.
#	2. Verify that setting the last normal child to fallback is refused.
#	3. Verify that setting write_mode=behind on that child is also refused.
#	4. Check the properties and the refusal messages.
#

verify_runnable "global"
log_onexit cleanup_async_mirror

async_mirror_create_pool
log_must zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop read_mode)" == fallback

typeset output
output=$(zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_LEADER 2>&1) &&
	log_fail "the last normal member was allowed to become fallback"
case "$output" in
*"the mirror must keep a member with read_mode=normal and write_mode=sync"*) ;;
*) log_fail "the refusal does not name the required normal member: $output" ;;
esac
log_must test "$(get_async_mirror_prop read_mode $ASYNC_MIRROR_LEADER)" \
	== normal

output=$(zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LEADER 2>&1) &&
	log_fail "the last normal member was allowed to go behind"
case "$output" in
*"the mirror must keep a member with read_mode=normal and write_mode=sync"*) ;;
*) log_fail "the refusal does not name the required normal member: $output" ;;
esac
log_must test "$(get_async_mirror_prop read_mode $ASYNC_MIRROR_LEADER)" \
	== normal
log_must test "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LEADER)" \
	== sync

log_pass "A mirror keeps a normal, synchronous member."
