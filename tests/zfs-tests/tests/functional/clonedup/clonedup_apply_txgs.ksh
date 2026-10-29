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
#	The apply phase does not spin empty transaction groups.  The
#	sync thread skips its timeout while a scan is active, so an
#	apply with nothing to do each group would sync about a
#	hundred empty groups a second, each writing its own overhead.
#	The frees of an async destroy still sync back to back while
#	the apply waits, and on a pool with no run the apply thread
#	stays asleep.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable TXG_HISTORY
	log_must restore_tunable ASYNC_BLOCK_MAX_BLOCKS
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

function txg_open
{
	kstat_pool $TESTPOOL txgs |
	    awk 'NR > 1 { t = $1 } END { print t }'
}

# The voluntary context switches of the thread with the given pid
function apply_sleeps # pid
{
	awk '/^voluntary_ctxt_switches:/ { print $2 }' /proc/$1/status
}

log_assert \
    "the apply phase syncs transaction groups at the normal cadence"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED
log_must save_tunable ASYNC_BLOCK_MAX_BLOCKS
log_must save_tunable TXG_HISTORY
# The txgs kstat that txg_open reads stays empty while
# zfs_txg_history is 0, as other tests may leave it.
log_must set_tunable32 TXG_HISTORY 100

# The apply threads other pools started, so that the check below
# reads only the one this pool starts.
typeset others=" "
is_linux && others=" $(pgrep '^z_clonedup_ap' | tr '\n' ' ')"
clonedup_pool_create
clonedup_write /$TESTPOOL/a 64
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# With no run, this pool's apply thread does not wake.  Linux counts
# each thread's sleeps in /proc.
if is_linux; then
	typeset pid apid=""
	for pid in $(pgrep '^z_clonedup_ap'); do
		[[ $others == *" $pid "* ]] ||
		    apid="${apid:+$apid }$pid"
	done
	log_note "this pool's apply thread: $apid"
	log_must [ $(echo $apid | wc -w) -eq 1 ]
	typeset -i w0=$(apply_sleeps $apid)
	sleep 5
	typeset -i w1=$(apply_sleeps $apid)
	log_note "apply thread $apid slept $((w1 - w0)) times in 5 s"
	log_must [ $((w1 - w0)) -le 1 ]
fi

# park the run in its apply phase with nothing to do
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup -n $TESTPOOL
clonedup_wait_applying

typeset before=$(txg_open)
sleep 10
typeset after=$(txg_open)
log_note "open txg $before, 10 seconds later $after"
clonedup_assert_counts "$before" "$after"
log_must [ $before -gt 0 ]
# A run that finished would also let the sync thread wait out its
# timeout, so the run has to be still parked.
log_must eval "zpool status $TESTPOOL | grep -q applying"
typeset adone=$(clonedup_jstat clonedup_apply_done)
clonedup_assert_counts "$adone"
log_must [ $adone -eq 0 ]
log_note "$((after - before)) transaction groups opened in 10 seconds"
log_must [ $((after - before)) -le 4 ]

# An async destroy still frees in back-to-back groups while the run
# waits in its apply.  At 100 blocks a group, a file system of 2048
# blocks takes about 21 groups, which one group per zfs_txg_timeout
# would stretch past 100 seconds.
log_must zfs create -o recordsize=1k $TESTPOOL/frees
log_must file_write -o create -f /$TESTPOOL/frees/f -b 1024 -c 2048 \
    -d R
clonedup_sync
log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS 100
log_must zfs destroy $TESTPOOL/frees
typeset -i i
for ((i = 0; i < 300; i++)); do
	[[ $(zpool get -Hpo value freeing $TESTPOOL) == 0 ]] && break
	sleep 0.1
done
log_note "the destroy freed its blocks in $((i / 10)) seconds"
log_must [ $(zpool get -Hpo value freeing $TESTPOOL) -eq 0 ]
log_must eval "zpool status $TESTPOOL | grep -q applying"

log_must zpool clonedup -s $TESTPOOL
log_mustnot clonedup_is_running

log_pass \
    "the apply phase syncs transaction groups at the normal cadence"
