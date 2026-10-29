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
#	A block that a head takes by block cloning after its last
#	snapshot, from a dataset that snapshot does not cover, is not
#	held by the snapshot.  Its logical birth, the birth
#	dsl_dataset_block_kill() tests, is after the snapshot, and its
#	physical birth is older.  The walk indexes the block, and the
#	run redirects a copy of it onto it.
#
# STRATEGY:
#	1. Write f0 in a and snapshot b.  Clone f0 into b as f2 and
#	   remove f0, so f2 holds the only reference to its blocks.
#	2. Write f3 in b, a byte copy of f2.
#	3. Run clonedup.  Check that f3 shares every block with f2,
#	   and that nothing counts as held by snapshots.
#

verify_runnable "global"

log_assert "a block cloned in after the last snapshot takes part"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must zfs create $TESTPOOL/b
clonedup_write /$TESTPOOL/a/f0
clonedup_sync
log_must zfs snapshot $TESTPOOL/b@s
clonedup_sync
log_must clonefile -f /$TESTPOOL/a/f0 /$TESTPOOL/b/f2
clonedup_sync
log_must rm /$TESTPOOL/a/f0
clonedup_dup /$TESTPOOL/b/f2 /$TESTPOOL/b/f3
clonedup_sync

clonedup_run
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_saved_snapheld 0
clonedup_check_shared $TESTPOOL/b f2 $TESTPOOL/b f3 \
    "$(clonedup_all_blocks)"
log_must cmp /$TESTPOOL/b/f2 /$TESTPOOL/b/f3
clonedup_leakcheck

log_pass "a block cloned in after the last snapshot takes part"
