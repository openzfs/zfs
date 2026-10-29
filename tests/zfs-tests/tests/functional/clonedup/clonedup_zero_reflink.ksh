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
#	An all-zero group becomes holes, and the copy kept as its
#	source is punched last, since nothing else refers to it.
#	When a reflink also refers to the source block, punching the
#	source frees nothing.  That block stays with the source and
#	the reflink, and neither saved nor the punched count
#	includes it.
#
# STRATEGY:
#	1. Write a file of zeros, reflink it, and copy it byte for
#	   byte.
#	2. Run a clonedup dry run, then a clonedup.
#	3. Check that the file and its reflink keep one shared data
#	   block and that every other block is a hole.
#	4. Check that each run counted only the blocks it freed.
#

verify_runnable "global"

# The DVAs of a file's blocks that hold data rather than a hole.
function cd_data_dvas # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ {
	    print $3 }'
}

log_assert "a reflinked zero source is kept and not counted"
log_onexit clonedup_cleanup
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
log_must dd if=/dev/zero of=/$TESTPOOL/z1 bs=$CD_BS \
    count=$CD_BLOCKS status=none
clonedup_sync
log_must clonefile -f /$TESTPOOL/z1 /$TESTPOOL/z1r
clonedup_dup /$TESTPOOL/z1 /$TESTPOOL/z2
clonedup_sync
clonedup_check_shared $TESTPOOL z1 $TESTPOOL z1r "$all"
clonedup_check_shared $TESTPOOL z1 $TESTPOOL z2 ""
typeset -i asz=$(clonedup_blk_asize $TESTPOOL z2)
log_must [ $asz -gt 0 ]

# z1's and z1r's references and z2's blocks, less the source
typeset -i want_applied=$((3 * CD_BLOCKS - 2))
# the other blocks z1 and z1r share, and z2's own
typeset -i want_saved=$(((2 * CD_BLOCKS - 1) * asz))

clonedup_run -n
log_must eval "zpool status -j $TESTPOOL |
    grep -q '\"dry_run\":true'"
typeset dry_applied=$(clonedup_jstat clonedup_applied)
typeset dry_saved=$(clonedup_jstat clonedup_saved)
typeset -i punched=$(clonedup_kstat punched)

clonedup_run
typeset applied=$(clonedup_jstat clonedup_applied)
typeset saved=$(clonedup_jstat clonedup_saved)
punched=$(( $(clonedup_kstat punched) - punched ))
log_note "dry run: $dry_applied blocks, $dry_saved reclaimable;" \
    "run: $applied blocks, $saved saved, $punched punched;" \
    "want $want_applied blocks, $want_saved bytes"
typeset v
for v in "$dry_applied" "$dry_saved" "$applied" "$saved"; do
	[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
done
log_must [ $applied -eq $want_applied ]
log_must [ $punched -eq $want_applied ]
log_must [ $saved -eq $want_saved ]
log_must [ $dry_applied -eq $applied ]
log_must [ $dry_saved -eq $saved ]

clonedup_sync
typeset d1=$(cd_data_dvas $TESTPOOL z1)
typeset d1r=$(cd_data_dvas $TESTPOOL z1r)
typeset d2=$(cd_data_dvas $TESTPOOL z2)
log_note "data blocks: z1 '$d1', z1r '$d1r', z2 '$d2'"
log_must [ $(echo $d1 | wc -w) -eq 1 ]
log_must [ "$d1" = "$d1r" ]
log_must [ -z "$d2" ]
log_must cmp /$TESTPOOL/z1 /$TESTPOOL/z2
log_must cmp /$TESTPOOL/z1r /$TESTPOOL/z2
clonedup_leakcheck

log_pass "a reflinked zero source is kept and not counted"
