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
#	A block that a snapshot holds frees nothing until the
#	snapshot goes, however many of its references a run
#	redirects.  When a snapshot holds one reference and a
#	dataset without snapshots holds a reflink, the run counts
#	the block as held by snapshots, whichever of the two has the
#	lower objset number.
#
# STRATEGY:
#	1. Write a file and run clonedup, so the next run matches
#	   against it as older data.
#	2. Create b, then copy the file into a.  Take a@s, again if
#	   needed, until its objset number is above b's.  Reflink
#	   the copy into b, and write one more block into a.
#	3. Run a clonedup dry run, then a clonedup.
#	4. Check that each run counted the copy's blocks once, all
#	   of them as held by snapshots.
#	5. Check that both references share the older file's
#	   blocks, and that the snapshot kept its own.
#

verify_runnable "global"

log_assert "a block a snapshot holds counts as held by snapshots"
log_onexit clonedup_cleanup
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must zfs create $TESTPOOL/b
clonedup_write /$TESTPOOL/o
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

clonedup_dup /$TESTPOOL/o /$TESTPOOL/a/x
clonedup_sync
typeset -i bobj=$(get_prop objsetid $TESTPOOL/b)
typeset -i sobj tries
for ((tries = 0; tries < 128; tries++)); do
	log_must zfs snapshot $TESTPOOL/a@s
	sobj=$(get_prop objsetid $TESTPOOL/a@s)
	(( sobj > bobj )) && break
	log_must zfs destroy $TESTPOOL/a@s
done
(( tries < 128 )) || log_fail "a@s never sorted after b"
log_must clonefile -f /$TESTPOOL/a/x /$TESTPOOL/b/x
# written since a@s, so the run may still rewrite a/x
clonedup_write /$TESTPOOL/a/n 1
clonedup_sync
clonedup_check_shared $TESTPOOL/a x $TESTPOOL/b x "$all"
log_note "objset of b $bobj, of a@s $sobj after $tries retries"
log_must [ $bobj -lt $sobj ]
typeset -i asz=$(clonedup_blk_asize $TESTPOOL/a x)
log_must [ $asz -gt 0 ]
typeset -i want=$((CD_BLOCKS * asz))

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL |
    grep -q '\"dry_run\":true'"
typeset dry_saved=$(clonedup_jstat clonedup_saved)
typeset dry_held=$(clonedup_jstat clonedup_saved_snapheld)

clonedup_run
typeset saved=$(clonedup_jstat clonedup_saved)
typeset held=$(clonedup_jstat clonedup_saved_snapheld)
log_note "dry run: $dry_saved saved, $dry_held of it held;" \
    "run: $saved saved, $held of it held; want $want, all held"
typeset v
for v in "$dry_saved" "$dry_held" "$saved" "$held"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done
log_must [ $saved -eq $want ]
log_must [ $held -eq $want ]
log_must [ $dry_saved -eq $want ]
log_must [ $dry_held -eq $want ]

clonedup_sync
clonedup_check_shared $TESTPOOL o $TESTPOOL/a x "$all"
clonedup_check_shared $TESTPOOL o $TESTPOOL/b x "$all"
clonedup_check_shared $TESTPOOL/a@s x $TESTPOOL/a x ""
clonedup_leakcheck

log_pass "a block a snapshot holds counts as held by snapshots"
