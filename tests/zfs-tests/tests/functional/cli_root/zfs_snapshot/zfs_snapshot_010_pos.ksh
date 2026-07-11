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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A recursive snapshot is taken atomically in a single txg, so every
#	resulting snapshot must report the same 'creation' time.
#
# STRATEGY:
#	1. Create a hierarchy with many child datasets.
#	2. Take a recursive snapshot of the whole hierarchy.
#	3. Verify every resulting snapshot shares one createtxg and one
#	   creation time.
#
# This is an invariant check on the fixed behavior, not a reproducer of the
# old per-dataset clock sampling.  That race only showed up when the in-kernel
# snapshot sync loop happened to straddle a one-second boundary, which is rare
# and not something a test can trigger reliably; here we just assert that a
# single recursive request collapses to one creation time by construction.
#

verify_runnable "both"

typeset PARENT=$TESTPOOL/crtime

function cleanup
{
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

log_assert "All snapshots from a recursive snapshot share one creation time"
log_onexit cleanup

log_must zfs create $PARENT

# Several children so the recursive snapshot spans many datasets.
for i in $(seq 1 50); do
	log_must zfs create $PARENT/child$i
done

log_must zfs snapshot -r $PARENT@snap

# The recursive snapshot must have covered the whole hierarchy: the parent
# plus its 50 children.
typeset nsnaps=$(zfs list -Hr -t snapshot -o name $PARENT | wc -l)
log_must test "$nsnaps" -eq 51

# The snapshots are atomic with respect to createtxg, so they already share
# one transaction group; check that first as a sanity guard on the hierarchy.
typeset txgs=$(zfs list -Hr -t snapshot -o createtxg -p $PARENT | sort -u |
    wc -l)
log_must test "$txgs" -eq 1

# With the fix every snapshot in the one recursive request also reports the
# same creation time, so the set of creation values collapses to exactly one.
typeset count=$(zfs list -Hr -t snapshot -o creation -p $PARENT | sort -u |
    wc -l)
log_note "distinct creation times across the recursive snapshot: $count"
log_must test "$count" -eq 1

log_pass "All snapshots from a recursive snapshot share one creation time"
