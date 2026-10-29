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
#	A volume open that finds the apply owning the volume waits for
#	it without spa_namespace_lock held, so pool commands go on in
#	the meantime.
#
# STRATEGY:
#	1. Write a file, then a volume with the same blocks.  The
#	   volume is closed, so the apply owns it as a destination.
#	2. Make the apply sleep 15 seconds per block while it owns the
#	   volume, and make a yield give up after 5 seconds.
#	3. Once the apply owns the volume, open it in the background.
#	4. A second later, time zpool list.  It must not wait for the
#	   open.
#

verify_runnable "global"

typeset open_pid=""

function cleanup
{
	[[ -n $open_pid ]] && wait $open_pid
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_YIELD_TIMEOUT_MS
	clonedup_cleanup
}

log_assert "a volume open waiting on the apply does not stall zpool"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_YIELD_TIMEOUT_MS

clonedup_pool_create
clonedup_write /$TESTPOOL/keep
clonedup_sync
clonedup_zvol_create $TESTPOOL/cv
clonedup_zvol_fill /$TESTPOOL/keep $TESTPOOL/cv
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 15000
log_must set_tunable32 CLONEDUP_YIELD_TIMEOUT_MS 5000
typeset -i owned=$(clonedup_kstat dst_owned)
typeset -i i
log_must zpool clonedup $TESTPOOL
for ((i = 0; i < 600; i++)); do
	(( $(clonedup_kstat dst_owned) > owned )) && break
	sleep 0.1
done
(( $(clonedup_kstat dst_owned) > owned )) ||
    log_fail "the apply never owned the volume"

dd if=$ZVOL_DEVDIR/$TESTPOOL/cv of=/dev/null bs=$CD_BS count=1 \
    status=none &
open_pid=$!
sleep 1
typeset -F SECONDS
typeset -F t0=$SECONDS took
log_must zpool list -H -o name $TESTPOOL
((took = SECONDS - t0))
log_note "zpool list took $took seconds during the volume open"
(( took < 2 )) ||
    log_fail "zpool list waited $took seconds on the volume open"

wait $open_pid
open_pid=""
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
clonedup_leakcheck

log_pass "a volume open waiting on the apply does not stall zpool"
