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
#	A read-only volume is not rewritten, a closed volume is
#	rewritten through ownership and serves as the surviving copy,
#	and a volume whose block size differs from the file never
#	matches.
#

verify_runnable "global"

function cleanup
{
	exec 3<&- 4<&-
	clonedup_cleanup
}

log_assert "volume skips: readonly, closed, block size mismatch"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
typeset nbytes=$((CD_BS * CD_BLOCKS))

# read-only volume with a duplicate of a file
clonedup_write /$TESTPOOL/a
clonedup_zvol_create $TESTPOOL/ro
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/ro
clonedup_sync
log_must zfs set readonly=on $TESTPOOL/ro

# closed volume with a duplicate of a file, plus a file that
# duplicates the closed volume (the volume is the older copy)
clonedup_zvol_create $TESTPOOL/closed
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/closed
clonedup_sync
clonedup_write /$TESTPOOL/c
clonedup_zvol_create $TESTPOOL/src
clonedup_zvol_fill /$TESTPOOL/c $TESTPOOL/src
clonedup_sync
log_must rm /$TESTPOOL/c
clonedup_sync
log_must eval "clonedup_zvol_read $TESTPOOL/src > /$TESTPOOL/d"
clonedup_sync

# volume with half the block size and the same bytes.  It never
# shares a key with the file, so this is a sanity check.
log_must zfs create -V $((CD_BS * CD_BLOCKS)) -b $((CD_BS / 2)) \
    $TESTPOOL/small
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/small
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/small
clonedup_sync

exec 3< $ZVOL_DEVDIR/$TESTPOOL/ro
exec 4<> $ZVOL_DEVDIR/$TESTPOOL/small

clonedup_run
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/ro ""
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/closed "$all"
clonedup_check_shared_vol $TESTPOOL a $TESTPOOL/small ""
clonedup_check_shared_vol $TESTPOOL d $TESTPOOL/src "$all"
clonedup_jstat_gt clonedup_skipped_policy 0
clonedup_jstat_is errors 0

exec 3<&- 4<&-
log_must eval "clonedup_zvol_read $TESTPOOL/closed |" \
    "cmp -n $nbytes - /$TESTPOOL/a"
clonedup_leakcheck

log_pass "volume skips: readonly, closed, block size mismatch"
