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
#	A clonedup walk keeps no scan queue object.  An import
#	restarts the run's partition from its first walk, so a
#	queue would never be read back, and a pool exported in the
#	middle of a run leaves nothing behind in the MOS.
#
# STRATEGY:
#	1. Run once, then copy old data.  A default run then indexes
#	   the copy and walks the pool again to match it.
#	2. Park that run in its apply phase, after both walks.
#	3. The MOS holds no scan work queue, and the scan record
#	   names none.
#	4. Export.  zdb finds no leaked MOS object.
#	5. Import, let the restarted run finish, and check it.
#	6. The MOS still holds no queue, and zdb finds no leak.
#	7. Scrub.  zdb names no field of the scrub's scan record.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	poolexists $TESTPOOL || zpool import $TESTPOOL >/dev/null 2>&1
	clonedup_cleanup
}

# How many scan work queues zdb lists in the MOS, or nothing when it
# lists no MOS.
function scan_queues
{
	typeset out i

	zpool sync $TESTPOOL >/dev/null 2>&1
	for ((i = 0; i < 5; i++)); do
		out=$(zdb -dd $TESTPOOL 2>/dev/null) && break
		sleep 1
	done
	[[ $out == *"Dataset mos [META]"* ]] || return 1
	print -r -- "$out" |
	    awk '/scan work queue/ { n++ } END { print n + 0 }'
}

# zdb's MOS leak check.  -e reads the exported pool.
function mos_leakcheck # [-e]
{
	typeset out
	typeset -i rc

	out=$(zdb $1 -d $TESTPOOL 2>&1)
	rc=$?
	if echo "$out" | grep -q "^MOS object .* leaked"; then
		log_note "$(echo "$out" | grep "^MOS object")"
		log_fail "zdb found leaked MOS objects in $TESTPOOL"
	fi
	((rc == 0)) ||
	    log_fail "zdb $1 -d $TESTPOOL exited $rc: $out"
}

log_assert "a clonedup walk keeps no scan queue object"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync
clonedup_run
clonedup_jstat_is clonedup_applied 0

clonedup_dup /$TESTPOOL/a /$TESTPOOL/c
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
log_must [ $(clonedup_kstat match_walks) -gt 0 ]

typeset qobj=$(scan_queues)
log_note "scan work queues in the MOS: '$qobj'"
log_must [ "$qobj" = "0" ]
qobj=$(clonedup_scan_record queue_obj)
log_note "the scan record's queue object: '$qobj'"
log_must [ "$qobj" = "0" ]

log_must_busy zpool export $TESTPOOL
mos_leakcheck -e
log_must zpool import $TESTPOOL

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_is state FINISHED
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_check_shared $TESTPOOL a $TESTPOOL c "$(clonedup_all_blocks)"

qobj=$(scan_queues)
log_note "scan work queues in the MOS after the run: '$qobj'"
log_must [ "$qobj" = "0" ]

log_must_busy zpool export $TESTPOOL
mos_leakcheck -e
log_must zpool import $TESTPOOL

# zdb shows the scan record by field only for a clonedup run
log_must zpool scrub -w $TESTPOOL
typeset out
out=$(zdb -dddd $TESTPOOL 1) ||
    log_fail "zdb -dddd $TESTPOOL 1 failed"
log_must eval \
    "print -r -- \"\$out\" | grep -qE '^[[:space:]]+scan = '"
log_mustnot eval \
    "print -r -- \"\$out\" | grep -qE '^[[:space:]]+queue_obj = '"

clonedup_leakcheck

log_pass "a clonedup walk keeps no scan queue object"
