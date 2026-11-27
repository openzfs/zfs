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
#	While the async mirror is on, detach, replace, attach, split and offline
#	of either child are refused.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write some data.
#	3. Verify that each command fails on each child.
#	4. Verify that the pool is unchanged and online.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 2
log_must wait_async_mirror_feature active
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_EXTRA

typeset child

for child in $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LEADER; do
	log_mustnot zpool detach $TESTPOOL $child
	log_mustnot zpool replace -f $TESTPOOL $child $ASYNC_MIRROR_EXTRA
	log_mustnot zpool attach -f $TESTPOOL $child $ASYNC_MIRROR_EXTRA
	log_mustnot zpool offline $TESTPOOL $child
	log_mustnot zpool offline -f $TESTPOOL $child
done
log_mustnot zpool split $TESTPOOL ${TESTPOOL}split

log_must check_state $TESTPOOL "" online
log_mustnot eval "zpool status $TESTPOOL | grep -q $ASYNC_MIRROR_EXTRA"
log_must test "$(get_async_mirror_stat active)" -eq 1

log_pass "Commands that change a child are refused while the async mirror" \
    "is on."
