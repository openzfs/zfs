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
#	A block that a reference the run never rewrites keeps frees
#	nothing when the run redirects its other references, so it
#	counts as neither saved nor held by snapshots.  Such a
#	reference is one in a clonedup=source dataset, or in a head
#	that nothing has written to since its latest snapshot.  A dry
#	run counts the same.
#
# STRATEGY:
#	1. Write o and o2 and run clonedup, so the next run matches
#	   against them as older data.
#	2. Copy o into a, snapshot a and reflink the copy into b,
#	   with a@s sorting after b.  Copy o2 into a clonedup=source
#	   dataset and reflink it into another, with the source
#	   sorting first.
#	3. Run a dry run, then a run.  Check that each redirects both
#	   reflinks onto o and o2 and counts nothing as saved.
#	4. Check that a and the source dataset kept their own blocks.
#

verify_runnable "global"

# Each reflink of CD_BLOCKS blocks is redirected and saves nothing.
function sk_counts
{
	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_applied $((2 * CD_BLOCKS))
	clonedup_jstat_is clonedup_saved 0
	clonedup_jstat_is clonedup_saved_snapheld 0
}

log_assert "a block a source-only reference keeps saves nothing"
log_onexit clonedup_cleanup
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
clonedup_write /$TESTPOOL/o
clonedup_write /$TESTPOOL/o2
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

# a keeps x unchanged after a@s, which sorts after b
log_must zfs create $TESTPOOL/b
log_must zfs create -o atime=off $TESTPOOL/a
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

# the source dataset sorts before the one its reflink goes to
log_must zfs create $TESTPOOL/p
log_must zfs create $TESTPOOL/q
typeset src=p dst=q
if (( $(get_prop objsetid $TESTPOOL/p) >
    $(get_prop objsetid $TESTPOOL/q) )); then
	src=q
	dst=p
fi
log_must zfs set clonedup=source $TESTPOOL/$src
clonedup_dup /$TESTPOOL/o2 /$TESTPOOL/$src/x
clonedup_sync
log_must clonefile -f /$TESTPOOL/$src/x /$TESTPOOL/$dst/x
clonedup_sync
clonedup_check_shared $TESTPOOL/a x $TESTPOOL/b x "$all"
clonedup_check_shared $TESTPOOL/$src x $TESTPOOL/$dst x "$all"

clonedup_run -n
clonedup_jstat_is clonedup_flags.dry_run true
sk_counts

clonedup_run
sk_counts
clonedup_sync
clonedup_check_shared $TESTPOOL o $TESTPOOL/b x "$all"
clonedup_check_shared $TESTPOOL o2 $TESTPOOL/$dst x "$all"
clonedup_check_shared $TESTPOOL o $TESTPOOL/a x ""
clonedup_check_shared $TESTPOOL o2 $TESTPOOL/$src x ""
log_must cmp /$TESTPOOL/o /$TESTPOOL/b/x
log_must cmp /$TESTPOOL/o2 /$TESTPOOL/$dst/x
clonedup_leakcheck

log_pass "a block a source-only reference keeps saves nothing"
