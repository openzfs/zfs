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
#	Saved counts each destination block once, however many of
#	its references the run redirects.  A file and its reflink
#	share every block.  Redirecting both onto an older copy frees
#	one copy of the data, and that is what saved reports, in a dry
#	run and in a real one.  Applied counts both references.
#
# STRATEGY:
#	1. Write a file and run clonedup, so the next run matches
#	   against it as older data.
#	2. Copy the file byte for byte, and reflink the copy.
#	3. Run a clonedup dry run, then a clonedup.
#	4. Check that each run applied two clones per block and saved
#	   one copy's allocated size per block.
#	5. Check that the copy and its reflink now share the older
#	   file's blocks.
#

verify_runnable "global"

log_assert "saved counts a block once for all its references"
log_onexit clonedup_cleanup
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
clonedup_write /$TESTPOOL/o
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

clonedup_dup /$TESTPOOL/o /$TESTPOOL/a
log_must clonefile -f /$TESTPOOL/a /$TESTPOOL/r
clonedup_sync
clonedup_check_shared $TESTPOOL a $TESTPOOL r "$all"
clonedup_check_shared $TESTPOOL o $TESTPOOL a ""
typeset -i asz=$(clonedup_blk_asize $TESTPOOL a)
log_must [ $asz -gt 0 ]

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
typeset dry_applied=$(clonedup_jstat clonedup_applied)
typeset dry_saved=$(clonedup_jstat clonedup_saved)

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
log_note "dry run: $dry_applied blocks, $dry_saved reclaimable;" \
    "run: $applied blocks, $saved saved; block asize $asz"
typeset v
for v in "$dry_applied" "$dry_saved" "$applied" "$saved"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done

# a and r each had their own pointer to every block
log_must [ $applied -eq $((2 * CD_BLOCKS)) ]
log_must [ $dry_applied -eq $applied ]
# but the blocks they pointed to are freed only once
log_must [ $saved -eq $((CD_BLOCKS * asz)) ]
log_must [ $dry_saved -eq $saved ]

clonedup_sync
clonedup_check_shared $TESTPOOL o $TESTPOOL a "$all"
clonedup_check_shared $TESTPOOL o $TESTPOOL r "$all"
clonedup_leakcheck

log_pass "saved counts a block once for all its references"
