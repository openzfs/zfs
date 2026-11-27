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
#	A fallback member serves reads only when the normal member cannot.
#	It can also provide data to a resilver.
#
# STRATEGY:
#	1. Create a mirror, disable data caching, and set one child to fallback.
#	2. Inject read errors on the fallback child.  A read succeeds from the
#	   normal child and the fallback child has no read errors.
#	3. Inject read errors on the normal child.  A read succeeds from the
#	   fallback child.
#	4. With read errors on the normal child, replace it, and verify that the
#	   resilver restores the data from the fallback child.
#

verify_runnable "global"

function cleanup_async_mirror_write_only
{
	zinject -c all > /dev/null 2>&1
	cleanup_async_mirror
}

function check_fallback_read_errors
{
	zpool status -j -L --json-int $TESTPOOL |
	    jq -e --arg pool "$TESTPOOL" \
	    --arg path "$ASYNC_MIRROR_LAGGING" '
	    [ .pools[$pool].vdevs | .. | objects |
		select(.path == $path) ] as $vdev |
	    ($vdev | length) == 1 and $vdev[0].read_errors == 0'
}

log_onexit cleanup_async_mirror_write_only

async_mirror_create_pool
log_must zfs set primarycache=none $TESTPOOL
log_must zpool set read_mode=fallback $TESTPOOL $ASYNC_MIRROR_LAGGING
async_mirror_write /$TESTPOOL/data 8
sync_pool $TESTPOOL
typeset digest=$(xxh128digest /$TESTPOOL/data)
typeset fallback_guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
typeset leader_guid=$(get_leaf_guid $ASYNC_MIRROR_LEADER)

log_must zinject -d $fallback_guid -e io -T read $TESTPOOL
log_must test "$(xxh128digest /$TESTPOOL/data)" == "$digest"
log_must zinject -c all
log_must check_fallback_read_errors

log_must zinject -d $leader_guid -e io -T read $TESTPOOL
log_must test "$(xxh128digest /$TESTPOOL/data)" == "$digest"
log_must zinject -c all

#
# The normal child is the last normal, synchronous member, so it cannot be
# taken offline.  Make its reads fail instead, so that the resilver of its
# replacement must read from the fallback member.
#
log_must zinject -d $leader_guid -e io -T read $TESTPOOL
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_EXTRA
log_must zpool replace -f $TESTPOOL $ASYNC_MIRROR_LEADER \
	$ASYNC_MIRROR_EXTRA
log_must wait_async_mirror_resilvered
log_must zinject -c all
log_must test "$(xxh128digest /$TESTPOOL/data)" == "$digest"
log_must test "$(get_async_mirror_prop read_mode)" == fallback

log_pass "The fallback member serves reads and a resilver."
