#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A RAID-Z expansion paused by an offlined child must resume by itself
#	once the child is back online and its resilver has completed,
#	without a "zpool clear".
#
# STRATEGY:
#	1. Create a raidz1 pool of file vdevs and fill it with data.
#	2. Attach a new child with the reflow held part-way.
#	3. Offline one child and release the reflow, so that it pauses
#	   ("paused for resilver or clear").
#	4. Write more data so the offlined child has a DTL, then online it
#	   and wait for its resilver to finish.
#	5. Without "zpool clear", wait for the expansion to complete.  Fail
#	   if it stays "paused for resilver or clear" for a bounded time.
#	   Then scrub and verify the data.
#	6. Repeat, since the lost wakeup depends on timing.
#

typeset -r devs=4
typeset -r dev_size_mb=256
typeset -r iters=3
typeset -r timeout=60
typeset -a disks

verify_runnable "global"

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"

	for i in {0..$devs}; do
		rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must restore_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES
	log_must restore_tunable SCRUB_AFTER_EXPAND
}

function dump_diagnostics # pool
{
	typeset pool=$1

	zpool status -v $pool
	zpool history -i $pool | grep -E 'reflow|resilver|vdev online' | \
	    tail -20
	if is_linux; then
		tail -40 /proc/spl/kstat/zfs/dbgmsg
	elif is_freebsd; then
		sysctl -n kstat.zfs.misc.dbgmsg | tail -40
	fi
}

#
# Wait, without "zpool clear", for the expansion to finish.  A slow copy
# is fine, but fail with diagnostics if the expansion stays paused for
# a resilver for more than $timeout seconds in a row.
#
function wait_expand_done_no_clear # pool timeout
{
	typeset pool=$1
	typeset -i timeout=$2
	typeset -i t0=$SECONDS
	typeset -i paused=0
	typeset st

	while st=$(zpool status $pool) &&
	    [[ "$st" == *"expansion of "*" in progress"* ]]; do
		if [[ "$st" == *"paused for resilver or clear"* ]]; then
			((paused += 1))
		else
			paused=0
		fi
		if ((paused > timeout)); then
			dump_diagnostics $pool
			log_fail "expansion of $pool still paused" \
			    "$timeout seconds after the resilver finished"
		fi
		sleep 1
	done
	log_note "expansion finished $((SECONDS - t0))s after resilver"
}

#
# Wait for the resilver of the onlined child to finish.  It is requested
# asynchronously, so "zpool wait -t resilver" alone may return before it
# has started.
#
function wait_resilvered # pool timeout
{
	typeset pool=$1
	typeset -i timeout=$2
	typeset -i t0=$SECONDS

	until zpool status $pool | grep -q 'scan: resilvered'; do
		if ((SECONDS - t0 > timeout)); then
			dump_diagnostics $pool
			log_fail "the onlined child of $pool was not" \
			    "resilvered within $timeout seconds"
		fi
		sleep 1
	done
}

function wait_expand_paused_for_resilver # pool timeout
{
	typeset pool=$1
	typeset -i timeout=$2
	typeset -i t0=$SECONDS

	until zpool status $pool | grep -q 'paused for resilver or clear'; do
		if ((SECONDS - t0 > timeout)); then
			dump_diagnostics $pool
			log_fail "expansion of $pool did not pause for the" \
			    "offlined child within $timeout seconds"
		fi
		sleep 1
	done
}

log_onexit cleanup

log_assert "RAID-Z expansion resumes after a resilver without zpool clear"

log_must save_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES
log_must save_tunable SCRUB_AFTER_EXPAND

# Keep the post-expansion scrub from masking the result.
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in {0..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
done

for (( iter = 0; iter < iters; iter++ )); do
	for i in {0..$devs}; do
		log_must truncate -s ${dev_size_mb}M ${disks[$i]}
	done
	log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 \
	    ${disks[0]} ${disks[1]} ${disks[2]} ${disks[3]}

	for f in {0..5}; do
		log_must dd if=/dev/urandom of=/$TESTPOOL/f$f bs=1M count=48 \
		    status=none
	done
	sync_pool $TESTPOOL
	typeset sums=$(cksum /$TESTPOOL/f*)

	# Hold the reflow at roughly a quarter of the allocated space.
	typeset -i alloc=$(get_pool_prop allocated $TESTPOOL)
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((alloc / 4))
	log_must zpool attach $TESTPOOL raidz1-0 ${disks[4]}
	wait_raidz_expand_paused $TESTPOOL

	# Let the reflow hit the offlined child and pause for it.
	log_must zpool offline $TESTPOOL ${disks[1]}
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	wait_expand_paused_for_resilver $TESTPOOL $timeout

	# Give the offlined child a DTL, then bring it back.
	log_must dd if=/dev/urandom of=/$TESTPOOL/extra bs=1M count=16 \
	    status=none
	sync_pool $TESTPOOL
	sums="$sums
$(cksum /$TESTPOOL/extra)"

	log_must zpool online $TESTPOOL ${disks[1]}
	wait_resilvered $TESTPOOL $timeout

	wait_expand_done_no_clear $TESTPOOL $timeout

	log_must zpool scrub -w $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	[[ "$(cksum /$TESTPOOL/f* /$TESTPOOL/extra)" == "$sums" ]] || \
	    log_fail "data changed across the expansion (iteration $iter)"

	log_must destroy_pool $TESTPOOL
done

log_pass "RAID-Z expansion resumes after a resilver without zpool clear"
