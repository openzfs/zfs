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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A read that fails while the apply opens a destination counts
#	as an error, for a file in a mounted filesystem and for one
#	the apply owns.  The failure says nothing about whether the
#	file changed, so it does not count as a stale skip.
#
# STRATEGY:
#	1. Write a file in one filesystem and a copy of it in each of
#	   two more with dnodesize=16k.  A 16K dnode fills a metadnode
#	   block, so no other dnode shares the block of either copy
#	   and a mount never reads it.  Export and import the pool, so
#	   no buffer of them stays cached.
#	2. Park a run in its apply phase.  Its walk has read those
#	   blocks by then, through the ARC alone.  A flush keeps a
#	   block the walk prefetched for 6 seconds, so wait that out.
#	3. Unmount the second filesystem, fail every read of the dnode
#	   block of each copy, and flush the ARC.  zinject gets each
#	   block by bookmark.  Given a path, it would look the file up
#	   and cache the block.
#	4. Let the apply go on.  It opens the first copy as a znode
#	   and the second by owning its filesystem.  Check that both
#	   faults fired, that nothing was cloned, and that every block
#	   of both copies counts as an error and none as a stale skip.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool clear $TESTPOOL >/dev/null 2>&1
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

# A file's object number, which ZFS reports as its inode number.
function oe_object # file
{
	ls -i $1 | awk '{ print $1 }'
}

#
# Fail every read of the metadnode block that holds object obj of
# ds, and print the handler's id.  A 16K block holds 32 dnode slots
# of 512 bytes.
#
function oe_inject # ds obj [-a]
{
	typeset os=$(zfs get -H -o value objsetid $1)

	zinject $3 -b $(printf "%x:0:0:%x" $os $(($2 / 32))) -e io \
	    -T read $TESTPOOL | awk '/^Added handler/ { print $3 }'
}

# How many reads the handler with this id failed.
function oe_injected # id
{
	zinject | awk -v id=$1 '$1 == id { print $NF }'
}

log_assert "a destination open that cannot read counts as an error"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create
typeset -A obj
typeset ds
typeset -i o

log_must zfs create $TESTPOOL/src
clonedup_write /$TESTPOOL/src/a
clonedup_sync
for ds in mnt own; do
	log_must zfs create -o dnodesize=16k $TESTPOOL/$ds
	clonedup_dup /$TESTPOOL/src/a /$TESTPOOL/$ds/b
	o=$(oe_object /$TESTPOOL/$ds/b)
	obj[$ds]=$o
	log_note "$ds/b is object $o"
	# a 16K dnode takes the 32 slots of one block
	(( o > 0 && o % 32 == 0 )) ||
	    log_fail "$ds/b is object $o, not alone in its block"
done
clonedup_sync
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

# The walk prefetched the dnode blocks.  A flush leaves a prefetched
# buffer in the ARC until it is older than
# zfs_arc_min_prescient_prefetch_ms, 6 seconds by default.
log_must sleep 8
log_must zfs unmount $TESTPOOL/own
typeset hown=$(oe_inject $TESTPOOL/own ${obj[own]})
typeset hmnt=$(oe_inject $TESTPOOL/mnt ${obj[mnt]} -a)
[[ -n $hown && -n $hmnt ]] || log_fail "zinject added no handler"
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL

# a handler that failed no read let a cached block through
typeset -i fown=$(oe_injected $hown) fmnt=$(oe_injected $hmnt)
log_note "reads failed: own/b $fown, mnt/b $fmnt"
(( fown > 0 && fmnt > 0 )) || log_fail "a dnode block was never read"
log_must zinject -c all
log_must zpool clear $TESTPOOL
log_must zfs mount $TESTPOOL/own

clonedup_check_shared $TESTPOOL/src a $TESTPOOL/mnt b ""
clonedup_check_shared $TESTPOOL/src a $TESTPOOL/own b ""
clonedup_jstat_is clonedup_applied 0
clonedup_jstat_is clonedup_skipped_stale 0
clonedup_jstat_is errors $((2 * CD_BLOCKS))
clonedup_leakcheck

log_pass "a destination open that cannot read counts as an error"
