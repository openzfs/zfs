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
#	An all-zero group whose destinations span several batches
#	becomes holes throughout, the surviving copy included.  Every
#	batch verifies its destinations against that copy, so it has
#	to stay in place until the last of them is done.  A dry run
#	reports the bytes the real run then saves.
#
# STRATEGY:
#	1. Write the same zero blocks into two datasets.
#	2. Apply on one worker, four blocks to a batch, so the
#	   destinations span batches and datasets.
#	3. Run a clonedup dry run, and check that every block still
#	   holds data.
#	4. Run a clonedup, and check that every block is a hole.
#	5. Check that the run applied and saved every block, the
#	   surviving copy included, and that the dry run counted the
#	   same blocks and bytes.
#	6. Check that the destinations took more than one batch.
#

verify_runnable "global"

typeset -ri cap=4
typeset -ri nzero=$((2 * CD_BLOCKS))

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

# How many of a file's blocks hold data rather than a hole.
function cd_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

log_assert "a zero group spanning batches becomes holes throughout"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset ds
for ds in a b; do
	log_must zfs create $TESTPOOL/$ds
	log_must dd if=/dev/zero of=/$TESTPOOL/$ds/z bs=$CD_BS \
	    count=$CD_BLOCKS status=none
	clonedup_sync
done
typeset -i asz=$(clonedup_blk_asize $TESTPOOL/a z)
log_note "zero block asize $asz"
log_must [ $asz -gt 0 ]

log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG $cap
log_must set_tunable32 TXG_TIMEOUT 1

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
typeset dry_applied=$(clonedup_jstat clonedup_applied)
typeset dry_saved=$(clonedup_jstat clonedup_saved)
typeset -i got
for ds in a b; do
	got=$(cd_data_blocks $TESTPOOL/$ds z)
	log_note "$ds/z after the dry run: $got data blocks," \
	    "want $CD_BLOCKS"
	log_must [ $got -eq $CD_BLOCKS ]
done

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
typeset punched=$(clonedup_kstat punched)
typeset batches=$(clonedup_kstat batches)
typeset v
for v in "$dry_applied" "$dry_saved" "$applied" "$saved" \
    "$punched" "$batches"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done

clonedup_sync
for ds in a b; do
	got=$(cd_data_blocks $TESTPOOL/$ds z)
	log_note "$ds/z after the run: $got data blocks, want 0"
	log_must [ $got -eq 0 ]
	got=$(stat_size /$TESTPOOL/$ds/z)
	log_note "$ds/z size $got, want $((CD_BLOCKS * CD_BS))"
	log_must [ $got -eq $((CD_BLOCKS * CD_BS)) ]
done
log_must cmp /$TESTPOOL/a/z /$TESTPOOL/b/z

typeset -i want=$((nzero * asz))
log_note "run: applied $applied, saved $saved;" \
    "want $nzero blocks, $want bytes"
log_must [ $applied -eq $nzero ]
log_must [ $saved -eq $want ]
log_note "kstat punched $punched, want $nzero"
log_must [ $punched -eq $nzero ]
log_note "dry run: applied $dry_applied, reclaimable $dry_saved;" \
    "run: applied $applied, saved $saved"
log_must [ $dry_applied -eq $applied ]
log_must [ $dry_saved -eq $saved ]
typeset bused=$(zpool get -Hpo value bcloneused $TESTPOOL)
log_note "bcloneused $bused, want 0"
log_must [ $bused -eq 0 ]

# fifteen destinations in two datasets, four to a batch
log_note "kstat batches $batches, want at least $((nzero / cap))"
log_must [ $batches -ge $((nzero / cap)) ]
clonedup_leakcheck

log_pass "a zero group spanning batches becomes holes throughout"
