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
#	A redirect of a block that a snapshot also holds leaves the
#	old block to the snapshot, so the head's USED grows by the
#	block's deflated size, less on raidz than the space the block
#	takes on disk.  A run keeps that growth within the quota and
#	counts what it leaves as skipped.  A dataset already at its
#	quota takes no clones, which counts as skipped and not as a
#	run error, and its all-zero blocks still become holes.
#
# STRATEGY:
#	1. On raidz1 over three disks, write a file and run clonedup,
#	   so the next run matches against it as older data.
#	2. Copy the file into q, snapshot q and write one more block
#	   there.  Leave q a quota above what it uses by one deflated
#	   block and half the gap between a block's allocated and
#	   deflated size, room for one redirect but not for the space
#	   a block takes on disk.
#	3. Copy the file into f, and write an all-zero file and a
#	   copy of it there.  Set f's quota to what f uses.
#	4. Run clonedup.
#	5. Check that the run redirected one of q's blocks, that q
#	   is within its quota and its copy is intact,
#	   that the run counted no errors and skipped what it
#	   refused, that f's copy kept its own blocks and that both
#	   zero files are holes throughout.
#

verify_runnable "global"

# How many of a file's blocks hold data rather than a hole.
function cd_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

log_assert "snapshot-held redirects stay within the quota"
log_onexit clonedup_cleanup

# Parity puts the space a block takes on disk above the deflated size
# a redirect adds to USED, so the quota below tells the two apart.
CD_VDEV=raidz1
CD_NDISKS=3
clonedup_pool_create
typeset ds
for ds in w q f; do
	log_must zfs create $TESTPOOL/$ds
done
clonedup_write /$TESTPOOL/w/o
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

clonedup_dup /$TESTPOOL/w/o /$TESTPOOL/q/h
clonedup_dup /$TESTPOOL/w/o /$TESTPOOL/f/d
log_must dd if=/dev/zero of=/$TESTPOOL/f/z1 bs=$CD_BS \
    count=$CD_BLOCKS status=none
clonedup_dup /$TESTPOOL/f/z1 /$TESTPOOL/f/z2
clonedup_sync
log_must zfs snapshot $TESTPOOL/q@s
# written since q@s, so the run may still rewrite q
clonedup_write /$TESTPOOL/q/n 1
clonedup_sync

typeset -i asz=$(clonedup_blk_asize $TESTPOOL/q h)
log_must [ $asz -gt 0 ]
# the deflated size, as dva_get_dsize_sync() derives it from the
# allocated size of a 128K block
typeset -i dsz=$(( (asz >> 9) * ((1 << 17) / (asz >> 9)) ))
log_must [ $dsz -lt $asz ]
# One redirect fits, with half the gap left for metadata.  The space
# a block takes on disk does not, whether the run sends q's blocks in
# one transaction or in several.
typeset -i qused=$(get_prop used $TESTPOOL/q)
typeset -i quota=$((qused + dsz + (asz - dsz) / 2))
log_must zfs set quota=$quota $TESTPOOL/q
typeset -i fused=$(get_prop used $TESTPOOL/f)
log_must zfs set quota=$fused $TESTPOOL/f
clonedup_sync
log_note "q uses $qused with quota $quota, f uses $fused," \
    "block asize $asz, dsize $dsz"

clonedup_run
clonedup_sync

qused=$(get_prop used $TESTPOOL/q)
log_note "q uses $qused after the run, quota $quota"
log_must [ $qused -le $quota ]
typeset -i nq=$(clonedup_shared $TESTPOOL/w o $TESTPOOL/q h | wc -w)
log_note "q/h shares $nq block(s) with w/o, want 1"
log_must [ $nq -eq 1 ]
log_must cmp /$TESTPOOL/w/o /$TESTPOOL/q/h
clonedup_jstat_is errors 0
# f's copy and most of q's were refused
clonedup_jstat_gt clonedup_skipped_policy $CD_BLOCKS
clonedup_check_shared $TESTPOOL/w o $TESTPOOL/f d ""
log_must cmp /$TESTPOOL/w/o /$TESTPOOL/f/d

typeset z
typeset -i got
for z in z1 z2; do
	got=$(cd_data_blocks $TESTPOOL/f $z)
	log_note "f/$z: $got data blocks, want 0"
	log_must [ $got -eq 0 ]
done
log_must cmp /$TESTPOOL/f/z1 /$TESTPOOL/f/z2
log_must [ $(get_prop used $TESTPOOL/f) -lt $fused ]
clonedup_leakcheck

log_pass "snapshot-held redirects stay within the quota"
