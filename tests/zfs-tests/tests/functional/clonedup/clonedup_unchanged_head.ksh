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
#	A run never rewrites a dataset that nothing has written to
#	since its latest snapshot, mounted or not.  All its data sits
#	in that snapshot, so a redirect there would only write past
#	the snapshot and grow its USED.  Its pairs are neither
#	candidates nor skips, and its blocks still serve as the
#	surviving copy for others.  A dataset written since its latest
#	snapshot still gets its snapshot-held blocks redirected, and a
#	clone written since it was made is a destination too.  A
#	snapshot taken while a run is in its apply, with nothing
#	written after it, turns its dataset's pairs into policy skips.
#	A dry run counts the same.
#
# STRATEGY:
#	1. Write a file and a byte copy of it into each of mt, um and
#	   ch, and one more copy of mt's file into x.  Snapshot mt, um
#	   and ch, clone mt@s as cc and copy mt's file into cc too.
#	   Write one more block into ch and unmount um.
#	2. Run a dry run, then a run.  Check that both count only the
#	   pairs of ch, x and cc, that ch's copy shares its blocks and
#	   counts as held by snapshots, that the copies in x and cc
#	   share mt's blocks, and that mt and um kept their blocks,
#	   their USED and a written of 0.
#	3. Do the same in w and wu, mounted and unmounted, but
#	   snapshot them again while a dry run, then a run, waits in
#	   its apply phase.  Check that each counts their pairs as
#	   policy skips and that nothing was written past the new
#	   snapshot.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

# Fail unless ds kept its copy b on blocks of its own and nothing was
# written to it after its snapshot snap.
function uh_untouched # ds snap
{
	clonedup_check_shared $1 a $1 b ""
	log_must [ "$(get_prop written@$2 $1)" = "0" ]
}

#
# Park a run with the flags given in its apply phase, snapshot w and
# wu under the name given with nothing written after, and let the run
# finish.  Each of their pairs then counts as a policy skip.
#
function uh_round # snap [flags]
{
	log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
	log_must zpool clonedup $2 $TESTPOOL
	clonedup_wait_applying
	log_must zfs snapshot $TESTPOOL/w@$1 $TESTPOOL/wu@$1
	clonedup_sync
	log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
	log_must zpool wait -t clonedup $TESTPOOL

	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_candidates $((2 * CD_BLOCKS))
	clonedup_jstat_is clonedup_applied 0
	clonedup_jstat_is clonedup_skipped_policy $((2 * CD_BLOCKS))
	uh_untouched $TESTPOOL/w $1
	uh_untouched $TESTPOOL/wu $1
}

log_assert "a dataset unchanged since its snapshot is never rewritten"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset ds

for ds in mt um ch; do
	log_must zfs create -o atime=off $TESTPOOL/$ds
	clonedup_write /$TESTPOOL/$ds/a
	clonedup_dup /$TESTPOOL/$ds/a /$TESTPOOL/$ds/b
done
log_must zfs create $TESTPOOL/x
clonedup_dup /$TESTPOOL/mt/a /$TESTPOOL/x/c
clonedup_sync
log_must zfs snapshot $TESTPOOL/mt@s $TESTPOOL/um@s $TESTPOOL/ch@s
log_must zfs clone $TESTPOOL/mt@s $TESTPOOL/cc
clonedup_dup /$TESTPOOL/mt/a /$TESTPOOL/cc/d
clonedup_write /$TESTPOOL/ch/n 1
clonedup_sync
log_must zfs unmount $TESTPOOL/um
typeset -i asz=$(clonedup_blk_asize $TESTPOOL/ch b)
log_must [ $asz -gt 0 ]
typeset mtused=$(get_prop used $TESTPOOL/mt)
typeset umused=$(get_prop used $TESTPOOL/um)

clonedup_run -n
clonedup_jstat_is clonedup_flags.dry_run true
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_candidates $((3 * CD_BLOCKS))
clonedup_jstat_is clonedup_applied $((3 * CD_BLOCKS))
clonedup_jstat_is clonedup_skipped_policy 0

clonedup_run
clonedup_sync
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_candidates $((3 * CD_BLOCKS))
clonedup_jstat_is clonedup_applied $((3 * CD_BLOCKS))
clonedup_jstat_is clonedup_skipped_policy 0
clonedup_jstat_is clonedup_saved_snapheld $((CD_BLOCKS * asz))
clonedup_check_shared $TESTPOOL/ch a $TESTPOOL/ch b "$all"
clonedup_check_shared $TESTPOOL/mt a $TESTPOOL/x c "$all"
clonedup_check_shared $TESTPOOL/mt a $TESTPOOL/cc d "$all"
uh_untouched $TESTPOOL/mt s
uh_untouched $TESTPOOL/um s
log_must [ "$(get_prop used $TESTPOOL/mt)" = "$mtused" ]
log_must [ "$(get_prop used $TESTPOOL/um)" = "$umused" ]

# written since w@s and wu@s, so the walk takes them as destinations
for ds in w wu; do
	log_must zfs create -o atime=off $TESTPOOL/$ds
	clonedup_write /$TESTPOOL/$ds/a
	clonedup_dup /$TESTPOOL/$ds/a /$TESTPOOL/$ds/b
done
clonedup_sync
log_must zfs snapshot $TESTPOOL/w@s $TESTPOOL/wu@s
for ds in w wu; do
	clonedup_write /$TESTPOOL/$ds/n 1
done
clonedup_sync
log_must zfs unmount $TESTPOOL/wu

uh_round d -n
clonedup_jstat_is clonedup_flags.dry_run true
log_must zfs destroy $TESTPOOL/w@d
log_must zfs destroy $TESTPOOL/wu@d
uh_round r
clonedup_leakcheck

log_pass "a dataset unchanged since its snapshot is never rewritten"
