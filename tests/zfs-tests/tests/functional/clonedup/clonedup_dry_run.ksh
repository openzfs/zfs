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
#	A dry run counts what it would clone, clones nothing, writes
#	nothing to the pool, and does not advance last_clonedup_txg,
#	so a real run afterwards still finds the duplicates.
#

verify_runnable "global"

# The birth of the MOS root block pointer, which moves with every
# write to the MOS.
function cd_dry_mos_birth
{
	zdb -u $TESTPOOL | awk '$1 == "bp" &&
	    match($0, /birth=[0-9]+L/) {
		print substr($0, RSTART + 6, RLENGTH - 7)
	}'
}

function cleanup
{
	log_must restore_tunable FREE_BPOBJ_ENABLED
	clonedup_cleanup
}

log_assert "zpool clonedup -n reports without cloning or writing"
log_onexit cleanup
log_must save_tunable FREE_BPOBJ_ENABLED

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# In every txg a scan runs in, dsl_process_async_destroys() dirties
# the free bpobj, empty or not, and that writes the MOS for any scan.
# With it off, only a write of the dry run's own moves the MOS root.
typeset fbe=$(get_tunable FREE_BPOBJ_ENABLED)
log_must set_tunable32 FREE_BPOBJ_ENABLED 0

# Let the writes above settle, so that only the run could move it.
typeset mos=$(cd_dry_mos_birth) now
typeset -i i
for ((i = 0; i < 10; i++)); do
	log_must zpool sync $TESTPOOL
	now=$(cd_dry_mos_birth)
	[[ $now == "$mos" ]] && break
	mos=$now
done
clonedup_assert_counts "$mos"
log_must [ "$now" = "$mos" ]

clonedup_run -n

# The run wrote nothing to the MOS, the dataset objects included.
log_must zpool sync $TESTPOOL
log_must [ "$(cd_dry_mos_birth)" = "$mos" ]
log_must set_tunable32 FREE_BPOBJ_ENABLED $fbe

clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
log_must [ "$(clonedup_jstat clonedup_candidates)" -eq $CD_BLOCKS ]
log_must [ "$(clonedup_jstat clonedup_applied)" -eq $CD_BLOCKS ]
log_must eval "zpool status -j $TESTPOOL | grep -q '\"dry_run\":true'"
log_must [ $(clonedup_last_txg) -eq 0 ]
log_must [ $(zpool get -Hpo value bcloneused $TESTPOOL) -eq 0 ]
log_must eval "zpool status $TESTPOOL | grep -q 'dry run completed'"
log_must eval \
    "zpool status -j $TESTPOOL | grep -q '\"state\":\"FINISHED\"'"

# the pool is exactly as the run found it
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" = "enabled" ]
typeset out
out=$(zdb -dddd $TESTPOOL 1) ||
    log_fail "zdb -dddd $TESTPOOL 1 failed"
log_mustnot eval "print -r -- \"\$out\" | grep -q clonedup_scan"
log_mustnot eval \
    "print -r -- \"\$out\" | grep -qE '^[[:space:]]+scan = '"
out=$(zpool history -i $TESTPOOL) ||
    log_fail "zpool history -i $TESTPOOL failed"
log_mustnot eval "print -r -- \"\$out\" | grep -q 'zpool clonedup'"
log_mustnot eval "print -r -- \"\$out\" | grep -q 'scan setup'"
log_mustnot eval "print -r -- \"\$out\" | grep -q 'ioctl scrub'"

# in memory only, so an import finds no run to report or resume
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
out=$(zpool status $TESTPOOL) ||
    log_fail "zpool status $TESTPOOL failed"
log_mustnot eval "print -r -- \"\$out\" | grep -q clonedup"

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
log_must [ $(clonedup_last_txg) -gt 0 ]
clonedup_leakcheck

log_pass "zpool clonedup -n reports without cloning or writing"
