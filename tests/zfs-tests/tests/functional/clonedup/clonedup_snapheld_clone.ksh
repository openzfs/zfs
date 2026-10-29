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
#	snapshot does not rank as held by that snapshot.  Its logical
#	birth, the birth dsl_dataset_block_kill() tests, is after the
#	snapshot, though its physical birth is older.  A copy that the
#	snapshot does hold survives over it, and the run frees the
#	cloned block.
#
# STRATEGY:
#	1. Write f0 in a, then f1 in b, a byte copy of f0, and
#	   snapshot b.
#	2. Clone f0 into b as f2.
#	3. Run clonedup.  Check that f0 and f2 share every block with
#	   f1 in b@s, and that nothing counts as held by snapshots.
#

verify_runnable "global"

log_assert "a block cloned in after the last snapshot is not held"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must zfs create $TESTPOOL/b
clonedup_write /$TESTPOOL/a/f0
clonedup_sync
clonedup_dup /$TESTPOOL/a/f0 /$TESTPOOL/b/f1
clonedup_sync
log_must zfs snapshot $TESTPOOL/b@s
clonedup_sync
log_must clonefile -f /$TESTPOOL/a/f0 /$TESTPOOL/b/f2
clonedup_sync

typeset all="$(clonedup_all_blocks)"
clonedup_run
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_applied $((2 * CD_BLOCKS))
clonedup_jstat_is clonedup_saved_snapheld 0
clonedup_check_shared $TESTPOOL/b@s f1 $TESTPOOL/a f0 "$all"
clonedup_check_shared $TESTPOOL/b@s f1 $TESTPOOL/b f2 "$all"
log_must cmp /$TESTPOOL/a/f0 /$TESTPOOL/b/f1
log_must cmp /$TESTPOOL/a/f0 /$TESTPOOL/b/f2
clonedup_leakcheck

log_pass "a block cloned in after the last snapshot is not held"
