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
#	"zpool set write_mode=behind" is refused where the async mirror cannot
#	work, and the property then stays off.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror.  Verify that the property
#	   is refused on the mirror vdev, and that a value other than off
#	   and lagging is refused.
#	2. Take a checkpoint.  Verify that lagging is refused.  Discard the
#	   checkpoint.
#	3. Turn the async mirror on.  Verify that lagging on the leader is refused.
#	4. Turn the async mirror off.  Attach a third child to the mirror, and wait
#	   for the resilver.  Verify that lagging is refused on a child of a
#	   three-way mirror.
#	5. Verify that the property of each child is off, and that the async mirror
#	   is off.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

function refused # vdev
{
	log_mustnot zpool set write_mode=behind $TESTPOOL $1
	log_must test "$(get_async_mirror_stat active)" -eq 0
}

async_mirror_create_pool
async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"

log_mustnot zpool set write_mode=behind $TESTPOOL mirror-0
log_mustnot zpool set write_mode=on $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync

log_must zpool checkpoint $TESTPOOL
refused $ASYNC_MIRROR_LAGGING
log_must zpool checkpoint -d -w $TESTPOOL

async_mirror_enable
log_mustnot zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LEADER
log_must test "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LEADER)" \
	    == sync
log_must test "$(get_async_mirror_prop state)" != off

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state off
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_EXTRA
log_must zpool attach $TESTPOOL $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_EXTRA
log_must wait_async_mirror_resilvered
refused $ASYNC_MIRROR_LAGGING
refused $ASYNC_MIRROR_EXTRA

for vdev in $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_EXTRA; do
	log_must test "$(get_async_mirror_prop write_mode $vdev)" == sync
	log_must test "$(get_async_mirror_prop state $vdev)" == off
done
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must verify_pool $TESTPOOL

log_pass "The property is refused where the async mirror cannot work."
