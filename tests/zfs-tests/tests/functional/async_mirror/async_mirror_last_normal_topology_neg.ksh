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
#	Mirror topology changes cannot remove the last normal, synchronous
#	member.
#
# STRATEGY:
#	1. Create a two-way mirror and make one member fallback-only.
#	2. Verify detach and offline of the normal member are refused.
#	3. Verify split cannot leave the original mirror with only a fallback.
#

verify_runnable "global"

function cleanup_async_mirror_last_normal_topology
{
	poolexists ${TESTPOOL}_split && destroy_pool ${TESTPOOL}_split
	cleanup_async_mirror
}

function check_last_normal_error # output operation
{
	case "$1" in
	*"read_mode=normal and write_mode=sync"*) ;;
	*) log_fail "$2 did not explain the required mirror member: $1" ;;
	esac
}

log_onexit cleanup_async_mirror_last_normal_topology

async_mirror_create_pool
log_must zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_LAGGING

typeset output
output=$(zpool detach $TESTPOOL $ASYNC_MIRROR_LEADER 2>&1) &&
	log_fail "detach removed the last normal, synchronous member"
check_last_normal_error "$output" detach
log_must test "$(get_async_mirror_prop read_mode \
	$ASYNC_MIRROR_LEADER)" == normal

output=$(zpool offline $TESTPOOL $ASYNC_MIRROR_LEADER 2>&1) &&
	log_fail "offline removed the last normal, synchronous member"
check_last_normal_error "$output" offline
log_must check_state $TESTPOOL $ASYNC_MIRROR_LEADER online

output=$(zpool split $TESTPOOL ${TESTPOOL}_split \
	$ASYNC_MIRROR_LEADER 2>&1) &&
	log_fail "split removed the last normal, synchronous member"
check_last_normal_error "$output" split
log_mustnot poolexists ${TESTPOOL}_split

log_pass "Topology changes keep a normal, synchronous mirror member."
