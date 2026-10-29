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
#	A resilver cancels a running clonedup; last_clonedup_txg does
#	not advance and a later run finishes the work.  A resilver
#	that an import needs also replaces a run found in progress,
#	and canceled_runs counts that run.
#

verify_runnable "global"

log_assert "a resilver cancels clonedup without losing progress state"
log_onexit clonedup_cleanup

set -A disks $DISKS
[[ ${#disks[@]} -lt 3 ]] && log_unsupported "need three disks"

log_must zpool create -f -o feature@clonedup=enabled \
    -O compression=off $TESTPOOL mirror ${disks[0]} ${disks[1]}
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must zpool replace $TESTPOOL ${disks[1]} ${disks[2]}
clonedup_release
log_must zpool wait -t resilver $TESTPOOL
clonedup_kstat_is canceled_runs 1
log_must [ $(clonedup_last_txg) -eq 0 ]

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

# A disk that was offline for a while needs a resilver at import,
# and the resilver replaces the run found in progress.
log_must zpool offline -t $TESTPOOL ${disks[2]}
clonedup_write /$TESTPOOL/c
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_sync
clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
clonedup_release
log_must zpool wait -t resilver $TESTPOOL
typeset func=$(zpool status -j $TESTPOOL |
    jq -r --arg p $TESTPOOL '.pools[$p].scan_stats.function')
log_note "scan on record after the import: $func"
log_must [ "$func" = "RESILVER" ]
clonedup_kstat_is canceled_runs 1
clonedup_run
clonedup_check_shared $TESTPOOL c $TESTPOOL d "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "a resilver cancels clonedup without losing progress state"
