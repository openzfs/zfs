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
#	A sequential rebuild reads a complete fallback member before an
#	incomplete normal member.
#
# STRATEGY:
#	1. Create a three-way mirror and make one member fallback-only.
#	2. Make a normal member miss writes, then bring it online during a
#	   paused sequential rebuild.
#	3. Fail reads from the other normal member and resume the rebuild.
#	4. Import the rebuilt member alone and verify all data.
#

verify_runnable "global"

function cleanup_async_mirror_rebuild_fallback
{
	if poolexists $TESTPOOL; then
		zpool export $TESTPOOL >/dev/null 2>&1
	fi
	if [[ -e $TEST_BASE_DIR/async_mirror_away/leader ]]; then
		mv $TEST_BASE_DIR/async_mirror_away/leader \
		    $ASYNC_MIRROR_LEADER
	fi
	if [[ -e $TEST_BASE_DIR/async_mirror_away/lagging ]]; then
		mv $TEST_BASE_DIR/async_mirror_away/lagging \
		    $ASYNC_MIRROR_LAGGING
	fi
	if [[ -e $TEST_BASE_DIR/async_mirror_away/extra ]]; then
		mv $TEST_BASE_DIR/async_mirror_away/extra $ASYNC_MIRROR_EXTRA
	fi
	if ! poolexists $TESTPOOL; then
		zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL >/dev/null 2>&1
	fi
	cleanup_async_mirror
	[[ ! -d $TEST_BASE_DIR/async_mirror_away ]] ||
	    log_must rmdir $TEST_BASE_DIR/async_mirror_away
}

log_onexit cleanup_async_mirror_rebuild_fallback

async_mirror_create_pool
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE \
	$ASYNC_MIRROR_EXTRA $ASYNC_MIRROR_REPLACEMENT
log_must zpool attach -f $TESTPOOL $ASYNC_MIRROR_LEADER \
	$ASYNC_MIRROR_EXTRA
log_must wait_async_mirror_resilvered
log_must zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_EXTRA

async_mirror_write_files before 2
log_must zpool offline $TESTPOOL $ASYNC_MIRROR_LAGGING
async_mirror_write_files after 2
typeset expected="$(async_mirror_digests before 2)
$(async_mirror_digests after 2)"

log_must async_mirror_set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s -f $TESTPOOL $ASYNC_MIRROR_LEADER \
	$ASYNC_MIRROR_REPLACEMENT
log_must zpool online $TESTPOOL $ASYNC_MIRROR_LAGGING
typeset leader_guid=$(get_leaf_guid $ASYNC_MIRROR_LEADER)
log_must zinject -d $leader_guid -e io -T read $TESTPOOL
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must wait_async_mirror_resilvered
log_must zinject -c all

log_must zpool export $TESTPOOL
log_must mkdir -p $TEST_BASE_DIR/async_mirror_away
log_must mv $ASYNC_MIRROR_LEADER $TEST_BASE_DIR/async_mirror_away/leader
log_must mv $ASYNC_MIRROR_LAGGING $TEST_BASE_DIR/async_mirror_away/lagging
log_must mv $ASYNC_MIRROR_EXTRA $TEST_BASE_DIR/async_mirror_away/extra
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_REPLACEMENT \
	$TESTPOOL
log_must test "$expected" == "$(async_mirror_digests before 2)
$(async_mirror_digests after 2)"
log_must zpool export $TESTPOOL

log_must mv $TEST_BASE_DIR/async_mirror_away/leader $ASYNC_MIRROR_LEADER
log_must mv $TEST_BASE_DIR/async_mirror_away/lagging $ASYNC_MIRROR_LAGGING
log_must mv $TEST_BASE_DIR/async_mirror_away/extra $ASYNC_MIRROR_EXTRA
log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL

log_pass "A rebuild uses a complete fallback member as its source."
