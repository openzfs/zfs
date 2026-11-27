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
#	A failed replace -s leaves a frozen lagging child unchanged.
#
# STRATEGY:
#	1. Freeze the lagging child with a damaged metadata block.
#	2. Run replace -s and check that it fails before the mode changes.
#	3. Turn the mode off and let the resulting resilver finish.
#	4. Scrub.  The leader lost every copy of a metadata block of the
#	   file before the lagging child got it, so the file is lost by
#	   design; verify that it is the only damage.
#

verify_runnable "global"

function cleanup_async_mirror_replace_frozen
{
	cleanup_async_mirror
	log_must rm -f $TEST_BASE_DIR/async_mirror_replace_frozen.err
}

log_onexit cleanup_async_mirror_replace_frozen

async_mirror_create_pool
async_mirror_enable
async_mirror_damage_backlog /$TESTPOOL/frozen 1 0
log_must wait_async_mirror_frozen

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_REPLACEMENT
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 1
typeset replace_error=$TEST_BASE_DIR/async_mirror_replace_frozen.err
if zpool replace -s -f $TESTPOOL $ASYNC_MIRROR_LAGGING \
    $ASYNC_MIRROR_REPLACEMENT >$replace_error 2>&1; then
	log_fail "replace -s succeeded while its switch-off resilvered"
fi
grep -Fq 'a failed or frozen lagging child cannot be replaced with -s' \
	$replace_error ||
	log_fail "replace -s returned the wrong error" "$(cat $replace_error)"
log_must test "$(get_async_mirror_prop state)" == frozen
log_must test "$(get_async_mirror_prop write_mode)" == behind

if async_mirror_child_has_flag replace; then
	log_fail "the failed replace left its pool marker behind"
fi

log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state off $ASYNC_MIRROR_TIMEOUT \
	$ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep -q '/$TESTPOOL/frozen'"
log_mustnot eval "zpool status -v $TESTPOOL | \
    awk '/^errors:/ { e = 1; next } e && NF' | grep -v '/$TESTPOOL/frozen'"

log_pass "A frozen child keeps a retryable async mirror after replace -s."
