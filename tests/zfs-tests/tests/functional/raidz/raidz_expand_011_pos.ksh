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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A RAIDZ expansion that pauses because a child is offline must
#	resume once that child is back online and resilvered.  The resilver
#	of a short outage often ends while the expansion thread is still on
#	its way to pausing a second time, after the online; the DTL
#	reassessment at the end of that resilver must still resume it.
#
#	Whether the resilver ends in that window depends on timing, so the
#	outage is repeated a few times, and a regression is not caught on
#	every run.
#
# STRATEGY:
#	1. Create a raidz1 pool, write some data, and throttle the expansion
#	   with raidz_expand_max_reflow_bytes
#	2. Attach a disk and wait for the throttle
#	3. Several times:
#	   - offline a child, write a little, raise the throttle, and wait
#	     for the expansion to pause for resilver
#	   - online the child and wait for its resilver
#	   - verify that the expansion does not stay paused for resilver
#	4. Lift the throttle, wait for the expansion, scrub, and verify the
#	   pool
#

typeset -r devs=5
typeset -r dev_size_mb=128
typeset -r cycles=4

typeset -a disks

original_scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must set_tunable32 SCRUB_AFTER_EXPAND $original_scrub_after_expand
}

function expanding # pool
{
	zpool status "$1" | grep -q 'expansion of .* in progress'
}

function paused_for_resilver # pool
{
	zpool status "$1" | grep -q 'paused for resilver or clear'
}

function reflowed # pool
{
	zpool status -jp --json-int "$1" | \
	    jq --arg pool "$1" '.pools[$pool].raidz_expand_stats.reflowed'
}

log_onexit cleanup

for i in {1..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${disks[$i]}
done

log_must set_tunable32 SCRUB_AFTER_EXPAND 0

log_must zpool create -f -o cachefile=none $TESTPOOL raidz1 \
    ${disks[1..$((devs - 1))]}
log_must dd if=/dev/urandom of=/$TESTPOOL/data bs=1M count=128
log_must zpool sync $TESTPOOL

# Stop once past the scratch phase, then once per outage.  What the
# scratch phase copies is not counted, so stay well below the total.
reflow_size=$(get_pool_prop allocated $TESTPOOL)
step=$((reflow_size / (cycles + 3)))
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $step
log_must zpool attach $TESTPOOL raidz1-0 ${disks[$devs]}
wait_raidz_expand_paused $TESTPOOL

child=${disks[2]}
typeset -i paused=0
for i in {1..$cycles}; do
	expanding $TESTPOOL || break
	log_must zpool offline $TESTPOOL $child
	log_must dd if=/dev/urandom of=/$TESTPOOL/outage-$i bs=1M count=1
	log_must zpool sync $TESTPOOL

	# The throttle is compared with the bytes copied in synced txgs, so
	# the reflow stops past it: raise it from what has been copied.
	copied=$(reflowed $TESTPOOL)
	[[ $copied == +([0-9]) ]] || log_fail "outage $i: reflowed '$copied'"
	log_note "outage $i: $copied bytes reflowed"
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((copied + step))

	typeset -i t0=$SECONDS
	while expanding $TESTPOOL && ! paused_for_resilver $TESTPOOL; do
		if ((SECONDS - t0 > 60)); then
			log_fail "outage $i: expansion did not pause for" \
			    "resilver in 60 seconds"
		fi
		sleep 1
	done
	paused_for_resilver $TESTPOOL && ((paused += 1))

	log_must zpool online $TESTPOOL $child
	log_must zpool wait -t resilver $TESTPOOL

	# The clock starts once no resilver is left to wait for.
	t0=$SECONDS
	while paused_for_resilver $TESTPOOL; do
		is_pool_resilvering $TESTPOOL && t0=$SECONDS
		if ((SECONDS - t0 > 30)); then
			log_fail "outage $i: expansion still paused for" \
			    "resilver 30 seconds after the resilver ended"
		fi
		sleep 1
	done
	wait_raidz_expand_paused $TESTPOOL
done

((paused > 0)) || log_fail "no outage paused the expansion"
log_note "$paused outages paused the expansion"

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

log_pass "an expansion paused on an offline child resumes after its resilver"
