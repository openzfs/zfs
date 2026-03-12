#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2024. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	L2ARC parallel writes sustain throughput over time without degradation.
#
# STRATEGY:
#	1. Disable DWPD rate limiting and depth cap.
#	2. Create pool without cache devices, fill ARC to arc_max.
#	3. Add 2 cache devices after ARC is full and stable.
#	4. Measure L2ARC throughput over 3 consecutive windows.
#	5. Verify throughput remains stable (no window drops below 50%
#	   of the first).
#

verify_runnable "global"

log_assert "L2ARC parallel writes sustain throughput without degradation."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	restore_tunable L2ARC_WRITE_MAX
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_DWPD_LIMIT
	restore_tunable L2ARC_EXT_HEADROOM_PCT
	restore_tunable ARC_MIN
	restore_tunable ARC_MAX
}
log_onexit cleanup

# Save original tunables
save_tunable L2ARC_WRITE_MAX
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_DWPD_LIMIT
save_tunable L2ARC_EXT_HEADROOM_PCT
save_tunable ARC_MIN
save_tunable ARC_MAX

# Test parameters — cache_sz and write_max are chosen so that total writes
# across all windows stay below the global marker reset threshold
# (smallest_capacity/8) to avoid throughput disruption from marker resets.
typeset cache_sz=3072
typeset window_time=10
typeset num_windows=3
typeset arc_max_mb=950
typeset fill_mb=$arc_max_mb

# Disable DWPD rate limiting and depth cap
log_must set_tunable32 L2ARC_DWPD_LIMIT 0
log_must set_tunable64 L2ARC_EXT_HEADROOM_PCT 0

# Set L2ARC_WRITE_MAX to 4MB/s per device
log_must set_tunable32 L2ARC_WRITE_MAX $((4 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure ARC size
log_must set_tunable64 ARC_MAX $((arc_max_mb * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((512 * 1024 * 1024))

# Create pool without cache devices
log_must truncate -s 5G $VDEV
log_must zpool create -f $TESTPOOL $VDEV

# Fill ARC to arc_max so eviction lists have stable evictable buffers
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb
log_must zpool sync $TESTPOOL

# Create and add cache devices now that ARC is full
typeset cache_devs=""
for letter in e f; do
	typeset dev="$VDIR/$letter"
	log_must truncate -s ${cache_sz}M $dev
	cache_devs="$cache_devs $dev"
done
log_must zpool add -f $TESTPOOL cache $cache_devs

# Wait for L2ARC to start writing
typeset l2_size=0
for i in {1..30}; do
	l2_size=$(kstat arcstats.l2_size)
	[[ $l2_size -gt 0 ]] && break
	sleep 1
done
if [[ $l2_size -eq 0 ]]; then
	log_fail "L2ARC did not start writing"
fi

# Measure throughput over consecutive windows
typeset -a window_bytes
for w in $(seq 1 $num_windows); do
	typeset start=$(kstat arcstats.l2_write_bytes)
	log_must sleep $window_time
	typeset end=$(kstat arcstats.l2_write_bytes)
	window_bytes[$w]=$((end - start))
	log_note "Window $w: $((window_bytes[$w] / 1024 / 1024))MB"
done

# First window must have non-trivial writes
if [[ ${window_bytes[1]} -le 0 ]]; then
	log_fail "No L2ARC writes in first window"
fi

# Each subsequent window must be at least 50% of the first
typeset min_bytes=$((window_bytes[1] * 50 / 100))
for w in $(seq 2 $num_windows); do
	if [[ ${window_bytes[$w]} -lt $min_bytes ]]; then
		log_fail "Window $w ($((window_bytes[$w] / 1024 / 1024))MB)" \
		    "degraded below 50% of window 1" \
		    "($((window_bytes[1] / 1024 / 1024))MB)"
	fi
done

destroy_pool $TESTPOOL

log_pass "L2ARC parallel writes sustain throughput without degradation."
