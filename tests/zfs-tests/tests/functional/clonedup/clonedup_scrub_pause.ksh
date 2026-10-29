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
#	zpool scrub -p pauses scrubs only.  With a clonedup run in
#	progress and no scrub, it has nothing to pause and leaves the
#	run going, the way it does on a pool with no scan at all.
#
# STRATEGY:
#	1. Hold a clonedup run in its first walk.
#	2. zpool scrub -p exits 0 and the run is neither paused nor
#	   gone.
#	3. zpool clonedup -p pauses it, and a plain zpool clonedup
#	   resumes it to the end.
#

verify_runnable "global"

log_assert "zpool scrub -p leaves a clonedup run alone"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running

log_must zpool scrub -p $TESTPOOL
log_must clonedup_is_running
log_mustnot clonedup_is_paused
log_mustnot is_pool_scrub_paused $TESTPOOL

log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
log_must zpool clonedup $TESTPOOL
log_mustnot clonedup_is_paused
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "zpool scrub -p leaves a clonedup run alone"
