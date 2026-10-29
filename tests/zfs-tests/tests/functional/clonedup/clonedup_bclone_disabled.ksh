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
#	With block cloning switched off by its tunable, clonedup
#	refuses to start with a message that names the tunable, and
#	nothing changes.  Switched off while a run applies, it stops
#	the cloning until it is switched back on, and the run then
#	finishes.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable BCLONE_ENABLED
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	clonedup_cleanup
}

log_assert "clonedup refuses to run while block cloning is disabled"
log_onexit cleanup
log_must save_tunable BCLONE_ENABLED
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

log_must set_tunable32 BCLONE_ENABLED 0
log_mustnot_expect zfs_bclone_enabled zpool clonedup $TESTPOOL
log_mustnot clonedup_is_running
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""

# A run already under way can still be paused and stopped.
log_must set_tunable32 BCLONE_ENABLED 1
clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must set_tunable32 BCLONE_ENABLED 0
log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
log_must zpool clonedup -s $TESTPOOL
clonedup_jstat_is state CANCELED
clonedup_release

# A run that is applying stops cloning while block cloning is
# disabled, and goes on once it is enabled again.
log_must set_tunable32 BCLONE_ENABLED 1
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 500
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
log_must set_tunable32 BCLONE_ENABLED 0
sleep 2
typeset -i clones=$(clonedup_kstat clones)
sleep 3
clonedup_kstat_is clones $clones
log_must clonedup_is_running
log_must set_tunable32 BCLONE_ENABLED 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must timeout 120 zpool wait -t clonedup $TESTPOOL
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"
clonedup_leakcheck

log_pass "clonedup refuses to run while block cloning is disabled"
