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
#	A rewind of the lagging child alone does not let it discard the
#	leader's newer txgs.  The leader moved on while the lagging child
#	was behind, and then went missing.  The lagging child alone is
#	imported with a rewind and written until its txg is above the
#	leader's.  The rewind discarded only the txgs that the lagging
#	child held, so when the leader returns, the read-write import is
#	refused, and the operator can keep the lagging child's copy.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a, wait until the lagging child has caught up, and note its
#	   newest txg R.  Write files c and wait again.
#	2. Stop the copy, write files b, and export the pool.  The leader is
#	   ahead of the lagging child.
#	3. Hide the leader.  Import the lagging child alone with -T R.  Write
#	   files d until the lagging child's newest txg is above the
#	   leader's, and export the pool.
#	4. Bring the leader back.  Verify that the import is refused as
#	   copies written separately.
#	5. Import with --async-mirror-keep and the guid of the lagging
#	   child.  Verify files a and d, that files b and c are gone, and
#	   wait for the resilver.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

# The rewind goes back over the copy of files c; see
# async_mirror_rewound_child_history_pos.
ASYNC_MIRROR_VDEV_SIZE=1g

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset -i rtxg=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
async_mirror_write_files c 2
log_must wait_async_mirror_idle
async_mirror_export_behind b 2
async_mirror_suspend_copy 0
typeset -i lag=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_note "rewind txg $rtxg, lagging child at $lag, leader at $leader"
log_must test $lag -gt $rtxg
log_must test $leader -gt $lag

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
async_mirror_import_txg $rtxg
log_mustnot test -e /$TESTPOOL/c.0
typeset -i i=0
while (( $(get_async_mirror_pool_txg) <= leader )); do
	(( i < 200 )) || log_fail "The pool txg stays at or below $leader"
	async_mirror_write /$TESTPOOL/d.$i 1
	sync_pool $TESTPOOL
	(( i++ ))
done
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests d $i)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -gt $leader

typeset out
out=$(zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
    log_fail "The import discarded the leader's txgs silently"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "The import failed with another error"
log_mustnot poolexists $TESTPOOL

log_must zpool import -d $ASYNC_MIRROR_DIR \
    --async-mirror-keep=$(get_leaf_guid $ASYNC_MIRROR_LAGGING) $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests d $i)"
log_mustnot test -e /$TESTPOOL/b.0
log_mustnot test -e /$TESTPOOL/c.0
log_must wait_async_mirror_resilvered
log_must verify_pool $TESTPOOL

log_pass "A rewind of the lagging child alone does not discard the " \
    "leader's newer txgs."
