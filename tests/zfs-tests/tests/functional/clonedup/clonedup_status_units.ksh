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
#	The walk's examined bytes and its total count the same thing,
#	each block pointer at its deflated size, so a block that
#	clones or the DDT share counts once for every pointer to it.
#	On a quiet pool a finished walk ends at or below its total,
#	short of it only by space no block pointer reaches.  The walk
#	leaves out a dataset with clonedup=off but still counts its
#	blocks.  A dry run reports what the real run then saves.
#
# STRATEGY:
#	1. Write a file, four clones of it and a byte copy, and the
#	   same data twice into a dedup=on dataset.
#	2. In a clonedup=off dataset write a file, snapshot it, then
#	   write a second file and clone the first.  Write a file into
#	   a clonedup=off clone of the snapshot.
#	3. Run a full clonedup dry run, then a full clonedup, on the
#	   quiet pool.
#	4. Check that the walk ends at or below its total, and within
#	   4 MiB of it.
#	5. Check that the apply and the partitions end complete.
#	6. Check that saved is the allocated size of every block the
#	   run redirected.
#	7. Check that the dry run counted the same blocks and bytes.
#

verify_runnable "global"

log_assert "walked, total and saved count in the same units"
log_onexit clonedup_cleanup

# 8 MiB in 128K records
typeset -ri n=64
#
# On a quiet pool the walk cannot reach the open intent log block,
# frees the last txgs have not returned, or MOS blocks rewritten
# after setup took the total.  Here that is far below 4 MiB.  A
# walk that counted the clones once would fall 32 MiB short, one
# that skipped the dedup blocks 16 MiB, and one that left the
# clonedup=off datasets out of its count 32 MiB.
#
typeset -ri margin=$((4 * 1024 * 1024))
typeset -i i

# One field of the pool's scan stats in $json.
function cd_scan # key
{
	echo "$json" | jq -r --arg p $TESTPOOL \
	    ".pools[\$p].scan_stats.$1"
}

clonedup_pool_create
clonedup_write /$TESTPOOL/a $n
for ((i = 1; i <= 4; i++)); do
	log_must clonefile -f /$TESTPOOL/a /$TESTPOOL/c$i
done
log_must zfs create -o dedup=on $TESTPOOL/dd
clonedup_write /$TESTPOOL/dd/d $n
clonedup_dup /$TESTPOOL/dd/d /$TESTPOOL/dd/e
# each of off@s, off and offc holds 8 MiB or more the walk skips
log_must zfs create -o clonedup=off $TESTPOOL/off
clonedup_write /$TESTPOOL/off/o $n
log_must zfs snapshot $TESTPOOL/off@s
clonedup_write /$TESTPOOL/off/p $n
log_must clonefile -f /$TESTPOOL/off/o /$TESTPOOL/off/q
log_must zfs clone -o clonedup=off $TESTPOOL/off@s $TESTPOOL/offc
clonedup_write /$TESTPOOL/offc/r $n
clonedup_sync
# b is younger than a, so the run keeps a's shared blocks
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

typeset -i asz_b=$(clonedup_blk_asize $TESTPOOL b)
typeset -i bsaved=$(zpool get -Hpo value bclonesaved $TESTPOOL)
typeset -i dsaved=$(zpool get -Hpo value dedupsaved $TESTPOOL)
log_note "bclonesaved $bsaved, dedupsaved $dsaved," \
    "block asize $asz_b"
log_must [ $asz_b -gt 0 ]
log_must [ $bsaved -gt $((2 * n * CD_BS)) ]
log_must [ $dsaved -gt $((n * CD_BS / 2)) ]

clonedup_run -n -f

typeset json=$(zpool status -j --json-int $TESTPOOL)
typeset dry=$(cd_scan clonedup_flags.dry_run)
typeset dry_applied=$(cd_scan clonedup_applied)
typeset dry_saved=$(cd_scan clonedup_saved)
log_note "dry run: $dry_applied blocks, $dry_saved reclaimable"
log_must [ "$dry" = "true" ]

clonedup_run -f

json=$(zpool status -j --json-int $TESTPOOL)
typeset func=$(cd_scan function) state=$(cd_scan state)
typeset walked=$(cd_scan examined) total=$(cd_scan to_examine)
typeset adone=$(cd_scan clonedup_apply_done)
typeset atotal=$(cd_scan clonedup_apply_total)
typeset part=$(cd_scan clonedup_partition)
typeset parts=$(cd_scan clonedup_partitions)
typeset applied=$(cd_scan clonedup_applied)
typeset saved=$(cd_scan clonedup_saved)
log_note "$func $state: walked $walked of $total, apply $adone of" \
    "$atotal, partition $part of $parts, applied $applied," \
    "saved $saved"
typeset v
for v in "$walked" "$total" "$adone" "$atotal" "$part" "$parts" \
    "$applied" "$saved" "$dry_applied" "$dry_saved"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done
log_must [ "$func" = "CLONEDUP" ]
log_must [ "$state" = "FINISHED" ]
log_must [ $walked -gt 0 ]

# the walk and its total count the same blocks
log_must [ $walked -le $total ]
log_must [ $((total - walked)) -le $margin ]

log_must [ $atotal -gt 0 ]
log_must [ $adone -eq $atotal ]
log_must [ $((part + 1)) -eq $parts ]

# b's blocks
typeset -i want=$((n * asz_b))
log_note "saved $saved, want $want"
log_must [ $applied -eq $n ]
log_must [ $saved -eq $want ]
log_must [ $dry_applied -eq $applied ]
log_must [ $dry_saved -eq $saved ]
clonedup_leakcheck

log_pass "walked, total and saved count in the same units"
