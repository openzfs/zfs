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
#	read_mode and write_mode support each valid cell of their mode table.
#
# STRATEGY:
#	1. Verify the normal, synchronous defaults.
#	2. Set read_mode=fallback on a synchronous child, then restore normal.
#	3. Set write_mode=behind and verify that it also sets read_mode=fallback.
#	4. Refuse read_mode=normal while the child is behind.
#	5. Set write_mode=sync and verify that fallback remains until reset.
#

verify_runnable "global"
log_onexit cleanup_async_mirror

async_mirror_create_pool
log_must test "$(get_async_mirror_prop read_mode $ASYNC_MIRROR_LEADER)" \
	== normal
log_must test "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LEADER)" \
	== sync
log_must test "$(get_async_mirror_prop read_mode)" == normal
log_must test "$(get_async_mirror_prop write_mode)" == sync

log_must zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop read_mode)" == fallback
log_must test "$(get_async_mirror_prop write_mode)" == sync
log_must zpool set read_mode=normal $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop read_mode)" == normal

async_mirror_enable
log_must test "$(get_async_mirror_prop read_mode)" == fallback
log_must test "$(get_async_mirror_prop write_mode)" == behind
typeset output
output=$(zpool set read_mode=normal $TESTPOOL $ASYNC_MIRROR_LAGGING 2>&1) &&
	log_fail "read_mode=normal was allowed on a behind member"
case "$output" in
*"read_mode=normal is not allowed while write_mode=behind"*) ;;
*) log_fail "the refusal does not explain the behind mode: $output" ;;
esac
log_must test "$(get_async_mirror_prop read_mode)" == fallback

async_mirror_disable
log_must test "$(get_async_mirror_prop read_mode)" == fallback
log_must test "$(get_async_mirror_prop write_mode)" == sync

log_must zpool set read_mode=normal $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop read_mode)" == normal

log_pass "The mirror accepts each valid read and write mode."
