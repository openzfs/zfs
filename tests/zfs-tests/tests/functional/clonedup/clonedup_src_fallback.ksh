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
#	When the source a run chose for a group is gone by the time
#	the apply reaches it, the apply takes a live copy of the group
#	as the source instead.  The copies still end up on one block.
#	The copy that took the source's place lost its pair, so the
#	run counts it as stale.  The run moves last_clonedup_txg past
#	these blocks, so a default run that left them apart would
#	never pair them again.
#
# STRATEGY:
#	1. Write o1 and o2 and finish a run, so both are older than
#	   the next run's window.
#	2. Copy each byte for byte into the root dataset and into a
#	   second dataset.
#	3. Park the next run in its apply phase.  Its match walk chose
#	   o1 and o2 as the sources.
#	4. Remove o1 and rewrite o2 with other data.
#	5. Let the apply run.
#	6. Check that each pair of copies shares every block, and that
#	   the run counted every block as a group and one copy of each
#	   as stale.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

log_assert "a group whose source went stale still shares one block"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED
typeset all="$(clonedup_all_blocks)"

clonedup_pool_create
log_must zfs create $TESTPOOL/fs
clonedup_write /$TESTPOOL/o1
clonedup_write /$TESTPOOL/o2
clonedup_sync
clonedup_run
log_must [ $(clonedup_last_txg) -gt 0 ]

clonedup_dup /$TESTPOOL/o1 /$TESTPOOL/a1
clonedup_dup /$TESTPOOL/o1 /$TESTPOOL/fs/b1
clonedup_dup /$TESTPOOL/o2 /$TESTPOOL/a2
clonedup_dup /$TESTPOOL/o2 /$TESTPOOL/fs/b2
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

# o1 goes, and o2 gets new blocks at the same offsets
log_must rm /$TESTPOOL/o1
log_must dd if=/dev/urandom of=/$TESTPOOL/o2 bs=$CD_BS \
    count=$CD_BLOCKS conv=notrunc status=none
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_check_shared $TESTPOOL a1 $TESTPOOL/fs b1 "$all"
clonedup_check_shared $TESTPOOL a2 $TESTPOOL/fs b2 "$all"
log_must cmp /$TESTPOOL/a1 /$TESTPOOL/fs/b1
log_must cmp /$TESTPOOL/a2 /$TESTPOOL/fs/b2
# one group per block of a1 and a2, one copy of each redirected,
# and the other stale
clonedup_jstat_is clonedup_groups $((2 * CD_BLOCKS))
clonedup_jstat_is clonedup_applied $((2 * CD_BLOCKS))
clonedup_jstat_is clonedup_skipped_stale $((2 * CD_BLOCKS))
clonedup_leakcheck

log_pass "a group whose source went stale still shares one block"
