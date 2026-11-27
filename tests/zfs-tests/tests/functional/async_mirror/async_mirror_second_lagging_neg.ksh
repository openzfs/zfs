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
#	This version supports one behind member in a pool.  "zpool set
#	write_mode=behind" on a second leaf is refused, with a message
#	that names the leaf that has the property already.  The command
#	never turns the property of that leaf off by itself: when a later
#	version supports more than one behind member, the same command adds
#	a behind member, so it must not have another meaning now.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Set write_mode=behind on the leader.  Verify that the command
#	   fails, that its message names the lagging child and tells the
#	   operator what to do, and that the property and the state of both
#	   children did not change.
#	3. Export the pool, and import the leader alone, so that the records
#	   are released.  Verify that the import set the missing lagging child
#	   to write_mode=sync (no behind leaf is left that the async mirror
#	   does not run on), and that the async mirror is not on.
#	4. Export, bring the lagging child back, import, wait for the
#	   resilver, and set write_mode=behind on the lagging child again.
#	5. Set write_mode=sync on the lagging child, and wait until the copy
#	   stops.  Set read_mode=normal on it.  Verify that
#	   write_mode=behind on the leader then works.
#

verify_runnable "global"

typeset errfile=$TEST_BASE_DIR/async_mirror_second_lagging.err

function cleanup_async_mirror_second_lagging
{
	cleanup_async_mirror
	log_must rm -f $errfile
}

log_onexit cleanup_async_mirror_second_lagging

#
# Verify that lagging on the leader is refused with the message that names
# the leaf guid, and that the properties did not change.
#
function refused_second # guid
{
	zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LEADER \
	    2>$errfile && log_fail "a second lagging leaf was accepted"
	log_note "$(cat $errfile)"
	log_must grep -q "$1" $errfile
	log_must grep -q 'one behind child' $errfile
	log_must grep -q 'write_mode=sync' $errfile
	log_must test "$(get_async_mirror_prop)" == behind
	log_must test "$(get_async_mirror_prop write_mode \
	    $ASYNC_MIRROR_LEADER)" == sync
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"
log_must wait_async_mirror_idle
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)

refused_second $guid
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(get_async_mirror_prop state)" != off
log_must test "$(get_async_mirror_prop state $ASYNC_MIRROR_LEADER)" == leader

log_must zpool export $TESTPOOL
async_mirror_import_leader
log_must wait_async_mirror_released
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_stat active)" -eq 0

log_must zpool export $TESTPOOL
async_mirror_unhide_lagging
async_mirror_import
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop write_mode)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
# The copy runs again after the resilver, so it stops first.
log_must wait_async_mirror_state off
log_must zpool set read_mode=normal $TESTPOOL $ASYNC_MIRROR_LAGGING
typeset -i i
for (( i = 0; i < ASYNC_MIRROR_TIMEOUT; i++ )); do
	zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LEADER && break
	zpool sync -l $TESTPOOL
	sleep 1
done
log_must test "$(get_async_mirror_prop write_mode \
	$ASYNC_MIRROR_LEADER)" == behind
log_must test "$(get_async_mirror_prop)" == sync
log_must wait_async_mirror_state "behind|current" $ASYNC_MIRROR_TIMEOUT \
    $ASYNC_MIRROR_LEADER

log_must test "$digests" == "$(async_mirror_digests a 2)"

log_pass "A second lagging leaf is refused, and the first one is kept."
