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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A config update whose txg suspends the pool does not hold
#	spa_namespace_lock while it waits: commands on another pool
#	keep working, zpool clear resumes the pool, and the update
#	then completes (#18648).
#
# STRATEGY:
#	1. Create a pool with failmode=wait and a second pool
#	2. Make the cache file write of the first pool fail, so that a
#	   config update is pending and retried by the async thread
#	3. Fail the label nvlist writes of its only vdev: ordinary txgs
#	   still sync, a txg that rewrites the vdev config suspends
#	4. Let the async thread retry the config update; the pool suspends
#	5. zpool status of both pools and zinject -c must return within
#	   the timeout; zpool clear must resume the pool, and the update
#	   must then write the cache file and the labels
#	6. Arm the label faults again and add a vdev: the config update
#	   of zpool add suspends the pool; check the same, and that the
#	   add then succeeds
#
# NOTES:
#	Every command that a regression could leave blocked runs in the
#	background, with its output in a file, and is waited for with a
#	timeout: the test runner waits for every process that holds its
#	output pipes, so one blocked process would stall the run.  Once a
#	command has timed out, nothing can resume the pool, and cleanup
#	leaves it.
#

verify_runnable "global"

log_assert "A config update does not hold the namespace lock while its" \
    "pool is suspended"

typeset DEVDIR=$TEST_BASE_DIR/failmode_config_update
typeset VDEV=$DEVDIR/vdev
typeset VDEV2=$DEVDIR/vdev2
typeset OTHER=$DEVDIR/other
typeset NODIR=$DEVDIR/nodir
typeset OUT=$TEST_BASE_DIR/failmode_config_update.out
typeset -i blocked=0
set -A bg_pids

function cleanup
{
	restore_tunable CCW_RETRY_INTERVAL
	# Once a command is blocked, so is every other one on any pool.
	if ((blocked == 0)) && bounded 60 zinject -c all &&
	    { ! poolexists $TESTPOOL || bounded 60 zpool clear $TESTPOOL; }
	then
		destroy_pool $TESTPOOL
		destroy_pool $TESTPOOL1
		rm -rf $DEVDIR $OUT
	else
		log_note "commands are blocked: leaving the pools"
	fi
}

# Total of the injection counts of the records on the pool
function injections
{
	zinject | awk -v pool=$TESTPOOL '
	    $1 ~ /^[0-9]+$/ && $2 == pool { n += $NF }
	    END { print n + 0 }'
}

# The txg of the first label of the pool's vdev
function label_txg
{
	zdb -l $VDEV | awk '$1 == "txg:" { print $2; exit }'
}

# Print where the given processes, the background commands, their
# children and the sync threads wait.
function print_stacks
{
	typeset pid
	typeset -a pids=("$@" "${bg_pids[@]}")

	for pid in "${pids[@]}"; do
		pids+=($(pgrep -P $pid))
	done
	pids+=($(pgrep -x txg_sync) $(pgrep -x spa_async))
	if is_linux && [[ -w /proc/sysrq-trigger ]]; then
		echo w > /proc/sysrq-trigger
	fi
	for pid in "${pids[@]}"; do
		kill -0 $pid 2>/dev/null || continue
		ps -o pid=,wchan=,args= -p $pid
		if is_linux; then
			cat /proc/$pid/stack
		elif is_freebsd; then
			procstat -kk $pid
		fi
	done
}

# Run a command for at most $1 seconds, and return its status
function bounded
{
	typeset -i timeout=$1
	shift

	"$@" > $OUT 2>&1 &
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if ((timeout-- == 0)); then
			blocked=1
			cat $OUT
			print_stacks $pid
			echo "$* did not return" >&2
			return 1
		fi
		sleep 1
	done
	wait $pid
	typeset -i status=$?
	cat $OUT
	return $status
}

# Wait up to $1 seconds for the commands started in the background
function wait_bg
{
	typeset -i timeout=$1
	typeset pid

	for pid in "${bg_pids[@]}"; do
		while kill -0 $pid 2>/dev/null; do
			if ((timeout-- == 0)); then
				blocked=1
				print_stacks
				return 1
			fi
			sleep 1
		done
	done
	return 0
}

function wait_suspended
{
	typeset -i tries=60

	until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
		if ((tries-- == 0)); then
			zinject
			log_fail "$TESTPOOL did not suspend"
		fi
		sleep 1
	done
}

function arm_label_faults # file
{
	log_must zinject -d $VDEV -e io -L nvlist $TESTPOOL
	log_must file_write -o create -f /$TESTPOOL/$1 -b 1048576 -c 8 -d R
	log_must bounded 60 zpool sync $TESTPOOL
	log_must test "$(kstat_pool $TESTPOOL state)" = "ONLINE"
	log_must test "$(injections)" -eq 0
}

#
# With the pool suspended under a config update, commands on both pools
# must answer, and zpool clear must resume it.
#
function clear_suspended
{
	log_must bounded 60 zpool status $TESTPOOL1
	log_must bounded 60 zpool status $TESTPOOL
	log_must bounded 60 zinject -c all
	log_must bounded 60 zpool clear $TESTPOOL
}

log_must save_tunable CCW_RETRY_INTERVAL
log_onexit cleanup

log_must mkdir -p $DEVDIR
log_must truncate -s $MINVDEVSIZE $VDEV $VDEV2 $OTHER
log_must zpool create -f -o failmode=wait $TESTPOOL $VDEV
log_must zpool create -f $TESTPOOL1 $OTHER

#
# zpool set cachefile refuses a missing directory: point the pool at one
# that exists, then remove it.  The config update that zpool set comment
# requests then fails to write the cache file, and the async thread
# retries it CCW_RETRY_INTERVAL seconds later.  That first write must
# succeed: a failed one already holds the update for that long.
#
log_must mkdir $NODIR
log_must zpool set cachefile=$NODIR/zpool.cache $TESTPOOL
log_must test -s $NODIR/zpool.cache
log_must rm -rf $NODIR
log_must zpool events -c
log_must zpool set comment=failmode_config_update $TESTPOOL
typeset -i tries=30
until zpool events | grep -q ereport.fs.zfs.config_cache_write; do
	((tries-- == 0)) && log_fail "the cache file write did not fail"
	sleep 1
done
log_must zpool sync $TESTPOOL

typeset -i ltxg=$(label_txg)
arm_label_faults file1

# The retry runs at the end of the next txg.
log_must set_tunable32 CCW_RETRY_INTERVAL 0
(file_write -o create -f /$TESTPOOL/file2 -b 1048576 -c 1 -d R &&
    zpool sync $TESTPOOL) > /dev/null 2>&1 &
bg_pids+=($!)
wait_suspended

log_must mkdir $NODIR
clear_suspended
wait_bg 60 || log_fail "$TESTPOOL did not sync after zpool clear"
log_must test "$(kstat_pool $TESTPOOL state)" = "ONLINE"
tries=60
until [[ -f $NODIR/zpool.cache ]]; do
	((tries-- == 0)) && log_fail "the config update did not complete"
	sleep 1
done
log_must test "$(label_txg)" -gt $ltxg

#
# zpool add: the txg of the add writes the labels of the new vdev only,
# and the config update that follows rewrites those of every vdev.
#
ltxg=$(label_txg)
arm_label_faults file3
zpool add -f $TESTPOOL $VDEV2 > $DEVDIR/add.out 2>&1 &
set -A bg_pids $!
wait_suspended

clear_suspended
wait_bg 60 || log_fail "zpool add did not return after zpool clear"
wait ${bg_pids[0]}
typeset -i rc=$?
log_note "zpool add returned $rc: $(<$DEVDIR/add.out)"
log_must test $rc -eq 0
log_must test "$(kstat_pool $TESTPOOL state)" = "ONLINE"
log_must eval "zpool status -P $TESTPOOL | grep -q $VDEV2"
log_must test "$(label_txg)" -gt $ltxg

log_pass "A config update does not hold the namespace lock while its" \
    "pool is suspended"
