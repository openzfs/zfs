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
#	A pair of copies that only snapshots hold is no candidate.
#	The apply maps a snapshot's block to the head of its dataset,
#	and a head that no longer holds the block leaves nothing to
#	redirect.  A full run counts such a pair neither as a group
#	nor as stale, and still counts and clones a live pair.
#
# STRATEGY:
#	1. Write a file into one dataset and copy it byte for byte
#	   into a second.  Snapshot both datasets, then remove both
#	   files.
#	2. Write a second file and a byte copy of it, both live.
#	3. Run a full clonedup.
#	4. Check that the run counted one group per live block,
#	   cloned each, and skipped nothing.
#

verify_runnable "global"

log_assert "pairs only snapshots hold are neither groups nor skips"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must zfs create $TESTPOOL/b
clonedup_write /$TESTPOOL/a/f
clonedup_dup /$TESTPOOL/a/f /$TESTPOOL/b/f
clonedup_sync
log_must zfs snapshot $TESTPOOL/a@s
log_must zfs snapshot $TESTPOOL/b@s
log_must rm /$TESTPOOL/a/f /$TESTPOOL/b/f
clonedup_write /$TESTPOOL/c
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_sync

clonedup_run -f

clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_groups $CD_BLOCKS
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_skipped_stale 0
clonedup_jstat_is clonedup_skipped_policy 0
clonedup_check_shared $TESTPOOL c $TESTPOOL d "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "pairs only snapshots hold are neither groups nor skips"
