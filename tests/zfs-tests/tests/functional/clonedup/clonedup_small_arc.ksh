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
#	The apply finishes with a small ARC.  Each clone in a
#	transaction counts a level-1 indirect block of memory, and
#	dmu_tx_assign() never assigns a transaction that needs more
#	memory than the ARC maximum.
#
# STRATEGY:
#	1. Hold the ARC to 128M.
#	2. Write a file of 8192 4K blocks and a byte copy of it.  One
#	   transaction for all of them would count 1G of indirect
#	   blocks.
#	3. Run clonedup.  It must finish within five minutes and
#	   share every block.
#

verify_runnable "global"

typeset -r CD_N=8192
typeset -r CD_SMALL_BS=4096
typeset -r CD_ARC_MIN=$((64 * 1024 * 1024))
typeset -r CD_ARC_MAX=$((128 * 1024 * 1024))
typeset -r CD_C_MIN=$(kstat arcstats.c_min)
typeset -r CD_C_MAX=$(kstat arcstats.c_max)

function cleanup
{
	typeset -i i

	# a transaction waiting on the small ARC assigns once it grows
	log_must set_tunable64 ARC_MAX $CD_C_MAX
	log_must set_tunable64 ARC_MIN $CD_C_MIN
	restore_tunable ARC_MAX
	restore_tunable ARC_MIN
	if [[ -n $CD_WEDGED_PID ]]; then
		for ((i = 0; i < 600; i++)); do
			kill -0 $CD_WEDGED_PID 2>/dev/null || break
			sleep 0.1
		done
		kill -0 $CD_WEDGED_PID 2>/dev/null || CD_WEDGED_PID=""
	fi
	clonedup_cleanup
}

log_assert "clonedup applies with a small ARC"
log_onexit cleanup

save_tunable ARC_MAX
save_tunable ARC_MIN
log_must set_tunable64 ARC_MIN $CD_ARC_MIN
log_must set_tunable64 ARC_MAX $CD_ARC_MAX
log_must [ "$(kstat arcstats.c_max)" -eq $CD_ARC_MAX ]

clonedup_pool_create
log_must zfs create -o recordsize=$CD_SMALL_BS $TESTPOOL/fs
clonedup_write /$TESTPOOL/fs/a $CD_N $CD_SMALL_BS
clonedup_dup /$TESTPOOL/fs/a /$TESTPOOL/fs/b
clonedup_sync

clonedup_bounded 300 zpool clonedup -w $TESTPOOL
log_note "batches: $(clonedup_kstat batches) for $CD_N clones"
clonedup_kstat_is clones $CD_N
clonedup_check_shared $TESTPOOL/fs a $TESTPOOL/fs b \
    "$(clonedup_all_blocks $CD_N)"
clonedup_leakcheck

log_pass "clonedup applies with a small ARC"
