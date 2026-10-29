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
#	The source of an all-zero group counts once, as a hole or as
#	a skip.  An immutable source keeps its block and counts as a
#	policy skip, in a dry run and in a run.
#
# STRATEGY:
#	1. Write an all-zero block to s/s and make it immutable, then
#	   write CD_BLOCKS all-zero blocks to a/z.  s/s is the older
#	   copy, so it is the group's source.
#	2. Run a dry run, then a run.
#	3. Check that each counted CD_BLOCKS blocks applied and one
#	   policy skip, so that applied and skipped add up to every
#	   copy of the block.
#	4. Check that a/z is all holes and that s/s still holds its
#	   block.
#

verify_runnable "global"

function cleanup
{
	if is_freebsd; then
		chflags noschg /$TESTPOOL/s/s 2>/dev/null
	else
		chattr -i /$TESTPOOL/s/s 2>/dev/null
	fi
	clonedup_cleanup
}

# How many of a file's blocks hold data rather than a hole.
function pc_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

log_assert "the source of an all-zero group counts once"
log_onexit cleanup

clonedup_pool_create
log_must zfs create $TESTPOOL/s
log_must zfs create $TESTPOOL/a
log_must dd if=/dev/zero of=/$TESTPOOL/s/s bs=$CD_BS count=1 \
    status=none
clonedup_sync
if is_freebsd; then
	log_must chflags schg /$TESTPOOL/s/s
else
	log_must chattr +i /$TESTPOOL/s/s
fi
log_must dd if=/dev/zero of=/$TESTPOOL/a/z bs=$CD_BS \
    count=$CD_BLOCKS status=none
clonedup_sync

clonedup_run -n
typeset dapplied=$(clonedup_jstat clonedup_applied)
typeset dskipped=$(clonedup_jstat clonedup_skipped)
typeset dcand=$(clonedup_jstat clonedup_candidates)
clonedup_assert_counts "$dapplied" "$dskipped" "$dcand"
log_note "dry run: $dcand candidates, $dapplied applied," \
    "$dskipped skipped"
log_must [ $dapplied -eq $CD_BLOCKS ]
log_must [ $dskipped -eq 1 ]
log_must [ $dcand -eq $CD_BLOCKS ]
log_must [ $(pc_data_blocks $TESTPOOL/a z) -eq $CD_BLOCKS ]

clonedup_run
typeset skipped=$(clonedup_jstat clonedup_skipped)
clonedup_assert_counts "$skipped"
log_must [ $skipped -eq 1 ]
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_skipped_policy 1
clonedup_jstat_is errors 0
clonedup_kstat_is punched $CD_BLOCKS
log_must [ $(pc_data_blocks $TESTPOOL/a z) -eq 0 ]
log_must [ $(pc_data_blocks $TESTPOOL/s s) -eq 1 ]
clonedup_leakcheck

log_pass "the source of an all-zero group counts once"
