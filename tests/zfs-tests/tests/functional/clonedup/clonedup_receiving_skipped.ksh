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
#	A dataset in the middle of a receive is neither indexed nor
#	written.  Once the receive completes, a full run picks it up.
#

verify_runnable "global"

typeset recv_pid=""
typeset stream=$TEST_BASE_DIR/clonedup_stream
typeset fifo=$TEST_BASE_DIR/clonedup_fifo

function cleanup
{
	exec 3>&- 2>/dev/null
	[[ -n $recv_pid ]] && kill $recv_pid 2>/dev/null
	rm -f $stream $fifo
	clonedup_cleanup
}

log_assert "a dataset being received takes no part in a run"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

log_must zfs create $TESTPOOL/src
clonedup_write /$TESTPOOL/src/a
clonedup_dup /$TESTPOOL/src/a /$TESTPOOL/src/b
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@s
log_must eval "zfs send $TESTPOOL/src@s > $stream"
# written since src@s, so the run may still rewrite src
clonedup_write /$TESTPOOL/src/n 1
typeset size=$(stat_size $stream)
typeset half=$((size / 2))

# feed half the stream, then hold the receive open
rm -f $fifo
log_must mkfifo $fifo
zfs recv $TESTPOOL/rcv < $fifo &
recv_pid=$!
exec 3>$fifo
log_must eval "head -c $half $stream >&3"
sleep 2
clonedup_sync

clonedup_run
clonedup_check_shared $TESTPOOL/src a $TESTPOOL/src b "$all"
clonedup_jstat_is clonedup_candidates $CD_BLOCKS
clonedup_jstat_is errors 0

# let the receive finish
log_must eval "tail -c +$((half + 1)) $stream >&3"
exec 3>&-
wait $recv_pid
typeset rc=$?
recv_pid=""
log_must [ $rc -eq 0 ]
log_must datasetexists $TESTPOOL/rcv@s
clonedup_check_shared $TESTPOOL/src a $TESTPOOL/rcv a ""

# written since rcv@s, so the run may still rewrite rcv
clonedup_write /$TESTPOOL/rcv/n 1
clonedup_sync
clonedup_run -f
clonedup_check_shared $TESTPOOL/src a $TESTPOOL/rcv a "$all"
clonedup_check_shared $TESTPOOL/src a $TESTPOOL/rcv b "$all"
log_must cmp /$TESTPOOL/src/a /$TESTPOOL/rcv/b
clonedup_leakcheck

log_pass "a dataset being received takes no part in a run"
