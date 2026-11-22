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
#	A failed DTL label flush keeps pool uberblocks off the child.  While
#	the failed child cannot flush its DTL label, newer pool uberblocks stay
#	off it.
#
# STRATEGY:
#	1. Create a pool, set a long txg timeout and a DTL delay, enable the
#	   async mirror, write two before files and wait until idle.  Record
#	   the child, label and uberblock txgs and the before digest.
#	2. Inject write errors on the lagging child, write a failed file, and
#	   wait until the copy fails.  Clear the errors, sync, and wait until
#	   the DTL txg is set.
#	3. Suspend the release, disable the DTL delay, and enable the DTL
#	   label flush injection.  Sync eight times.  Verify that a scrub does
#	   not run.
#	4. Check that the child label and uberblock txgs stay at or below the
#	   child txg while the leader uberblock txg passes the DTL txg.
#	5. Export the pool and recheck the child label and uberblock txgs.
#	6. Disable the injection, resume the release, hide the leader, and
#	   import read-only.  Verify the pool txg, the before digest, and that
#	   the failed file is absent.  Export, check with zdb, and unhide the
#	   leader.
#

verify_runnable "global"

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
log_must async_mirror_set_tunable32 TXG_TIMEOUT 600
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_DELAY 100
async_mirror_enable
async_mirror_write_files before 2
log_must wait_async_mirror_idle

typeset -i child_txg=$(get_async_mirror_stat applied_txg)
typeset -i child_label_txg=$(get_label_txg $ASYNC_MIRROR_LAGGING)
typeset -i child_ub_txg=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset before_digest=$(xxh128digest /$TESTPOOL/before.0)
log_must test $child_ub_txg -le $child_txg

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files failed 1
log_must wait_async_mirror_failed
log_must zinject -c all
sync_pool $TESTPOOL
log_must wait_async_mirror_stat dtl_txg -gt 0
typeset -i dtl_txg=$(get_async_mirror_stat dtl_txg)

# Keep the DTL marker while every attempted child label flush fails.
async_mirror_suspend_release 1
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_DELAY 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_LABEL_FLUSH 1
typeset -i i
for (( i = 0; i < 8; i++ )); do
	sync_pool $TESTPOOL
done
# While the DTL of the child grows, a scrub would count its missed blocks
# as checksum errors: it waits.
log_mustnot zpool scrub $TESTPOOL

typeset -i label_txg=$(get_label_txg $ASYNC_MIRROR_LAGGING)
typeset -i uberblock_txg=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader_ub_txg=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_must test $label_txg -eq $child_label_txg
log_must test $label_txg -lt $dtl_txg
log_must test $uberblock_txg -le $child_txg
log_must test $leader_ub_txg -gt $dtl_txg

log_must zpool export $TESTPOOL
label_txg=$(get_label_txg $ASYNC_MIRROR_LAGGING)
uberblock_txg=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_must test $label_txg -lt $dtl_txg
log_must test $uberblock_txg -le $child_txg

log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_LABEL_FLUSH 0
async_mirror_suspend_release 0
log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test $(get_async_mirror_pool_txg) -eq $child_txg
log_must test "$before_digest" == "$(xxh128digest /$TESTPOOL/before.0)"
log_mustnot test -e /$TESTPOOL/failed.0
log_must zpool export $TESTPOOL
log_must eval "zdb -e -p $ASYNC_MIRROR_DIR -bcc $TESTPOOL > /dev/null"
async_mirror_unhide_leader

log_pass "A failed child label flush keeps newer pool uberblocks off it."
