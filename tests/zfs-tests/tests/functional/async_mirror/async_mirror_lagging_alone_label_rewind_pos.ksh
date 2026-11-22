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
#	The lagging child alone can still go back to an older txg that it
#	holds, after the pool was imported with the async mirror on.  Each
#	such import writes the labels of the lagging child again.  A load
#	needs a label with a config txg at or below the txg that it selects,
#	so these labels must not follow the newest uberblock of the child.
#	Otherwise the last copy of a pool cannot be recovered with a rewind
#	when its newest txg is damaged.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Stop the release, so that no block of an older txg is used
#	   again.
#	2. Write files a, wait until the lagging child has caught up, and
#	   note its newest txg R.  Write files b and wait again.
#	3. Export and import the pool two times.  Each import turns the
#	   async mirror on again and writes the labels of the lagging
#	   child.  Export the pool.
#	4. Verify that the config txg in the labels of the lagging child
#	   is not above R.
#	5. Zero each uberblock above R on the lagging child, as if its
#	   newer txgs were lost.  Hide the leader.
#	6. Import the lagging child alone.  Verify that the pool is at txg
#	   R: it has files a, and it does not have files b.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

ASYNC_MIRROR_VDEV_SIZE=1g

async_mirror_create_pool
async_mirror_enable
async_mirror_suspend_release 1

async_mirror_write_files a 4
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 4)"
typeset -i rtxg=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
async_mirror_write_files b 2
log_must wait_async_mirror_idle

typeset -i i
for (( i = 0; i < 2; i++ )); do
	log_must zpool export $TESTPOOL
	async_mirror_import
	log_must test "$(get_async_mirror_stat active)" -eq 1
done
log_must zpool export $TESTPOOL

typeset -i ltxg=$(get_label_txg $ASYNC_MIRROR_LAGGING)
typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $rtxg, label txg $ltxg, newest uberblock txg $newest"
log_must test $newest -gt $rtxg
log_must test $ltxg -gt 0
log_must test $ltxg -le $rtxg

log_must async_mirror_zero_uberblocks $ASYNC_MIRROR_LAGGING $rtxg
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $rtxg

async_mirror_suspend_release 0
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 4)"
log_mustnot test -e /$TESTPOOL/b.0
log_must zpool export $TESTPOOL

log_pass "The lagging child alone can go back to an older txg after an" \
    "import with the async mirror on."
