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
#	Destroying a ZFS clone frees its blocks through its livelist.
#	After a run, the livelist holds block pointers the run
#	redirected onto a block of the origin, and pairs of pointers
#	to one block born in the clone.  The destroy frees each
#	reference once and leaks nothing.
#
# STRATEGY:
#	1. A file in fs, a snapshot of fs and a clone cl of it.  The
#	   clone keeps its livelist however little it shares.
#	2. In cl, copy the origin's file, and write a new file and a
#	   copy of that.
#	3. Run clonedup.  Each copy shares its original's blocks.
#	4. Destroy cl through its livelist and wait for the frees.
#	5. No BRT entry is left, and allocated is back where it was
#	   before the clone.
#	6. Write over the freed space, scrub, and compare the
#	   origin's file, so that a block freed twice shows.
#

verify_runnable "global"

typeset -r keep=$TEST_BASE_DIR/clonedup_cd_a

function cleanup
{
	log_must restore_tunable LIVELIST_MIN_PERCENT_SHARED
	rm -f $keep
	clonedup_cleanup
}

# How many BRT entries zdb lists, or nothing when zdb fails.
function cd_brt_entries
{
	typeset out

	out=$(zdb -TTTT $TESTPOOL) || return 1
	print -r -- "$out" |
	    awk '$1 ~ /^[0-9]+:[0-9a-f]+$/ && NF == 2 { n++ }
	    END { print n + 0 }'
}

log_assert "destroying a clone after a run frees every block once"
log_onexit cleanup
log_must save_tunable LIVELIST_MIN_PERCENT_SHARED
log_must set_tunable64 LIVELIST_MIN_PERCENT_SHARED -1

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset -ri margin=$((CD_BLOCKS * CD_BS / 2))
log_must zfs create $TESTPOOL/fs
clonedup_write /$TESTPOOL/fs/a
log_must cp /$TESTPOOL/fs/a $keep
clonedup_sync
log_must zfs snapshot $TESTPOOL/fs@s
clonedup_sync
typeset base=$(zpool get -Hpo value allocated $TESTPOOL)
log_must zfs clone $TESTPOOL/fs@s $TESTPOOL/cl
clonedup_dup /$TESTPOOL/cl/a /$TESTPOOL/cl/b
clonedup_write /$TESTPOOL/cl/c
clonedup_dup /$TESTPOOL/cl/c /$TESTPOOL/cl/d
clonedup_sync

clonedup_run
clonedup_check_shared $TESTPOOL/cl a $TESTPOOL/cl b "$all"
clonedup_check_shared $TESTPOOL/cl c $TESTPOOL/cl d "$all"

clonedup_sync
typeset used=$(zpool get -Hpo value bcloneused $TESTPOOL)
typeset nbrt=$(cd_brt_entries)
log_note "before the destroy: bcloneused $used, $nbrt BRT entries"
clonedup_assert_counts "$base" "$used" "$nbrt"
log_must [ $used -gt 0 ]
log_must [ $nbrt -gt 0 ]
log_must eval "zdb -vvvvv $TESTPOOL/cl | grep -q Livelist"

log_must zfs destroy $TESTPOOL/cl
log_must zpool wait -t free $TESTPOOL
# Freed space stays in allocated until it leaves the defer tree,
# two txgs after the free.
typeset i
for i in 1 2 3; do
	clonedup_sync
done

# Every BRT entry was cl's, so a reference the destroy did not drop
# stays visible, and so does a block it did not free.
used=$(zpool get -Hpo value bcloneused $TESTPOOL)
typeset saved=$(zpool get -Hpo value bclonesaved $TESTPOOL)
nbrt=$(cd_brt_entries)
typeset after=$(zpool get -Hpo value allocated $TESTPOOL)
log_note "after the destroy: bcloneused $used, bclonesaved $saved," \
    "$nbrt BRT entries, allocated $after (was $base)"
clonedup_assert_counts "$used" "$saved" "$nbrt" "$after"
log_must [ $used -eq 0 ]
log_must [ $saved -eq 0 ]
log_must [ $nbrt -eq 0 ]
log_must [ $((after - base)) -le $margin ]
log_must [ $((base - after)) -le $margin ]

# A destroy that freed a block of the origin lets new data land on
# it.  Write over the freed space and check the origin's file.
clonedup_write /$TESTPOOL/fs/n $((4 * CD_BLOCKS))
clonedup_sync
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'No known data errors'"
log_must cmp /$TESTPOOL/fs/.zfs/snapshot/s/a $keep
log_must cmp /$TESTPOOL/fs/a $keep
clonedup_leakcheck

log_pass "destroying a clone after a run frees every block once"
