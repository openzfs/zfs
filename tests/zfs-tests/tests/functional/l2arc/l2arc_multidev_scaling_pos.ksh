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
# Copyright (c) 2024. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	L2ARC parallel writes scale with number of cache devices.
#
# STRATEGY:
#	1. Configure L2ARC write rate to 4MB/s per device.
#	2. Disable DWPD rate limiting and depth cap to test pure parallel
#	   throughput.
#	3. Create a pool without cache devices and fill the ARC, so that the
#	   eviction lists hold a stable pool of evictable buffers.
#	4. Add a single cache device and measure the writes over 8s.
#	5. Detach it and repeat with two cache devices.
#	6. Verify that each phase reached its rate limit, and that two devices
#	   wrote substantially more than one.
#
# NOTES:
#	The ARC is filled before the cache devices are added, and nothing
#	writes to the pool while the window is measured.  A writer running
#	alongside makes the result depend on how fast it can feed the ARC
#	rather than on l2arc_write_max, and on a loaded machine the feed
#	threads then run out of eligible buffers.
#

verify_runnable "global"

log_assert "L2ARC parallel writes scale with number of cache devices."

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
# per phase stay below the global marker reset threshold
# (smallest_capacity/8) to avoid throughput disruption from marker resets.
typeset cache_sz=3072
typeset arc_max_mb=400
typeset fill_mb=$arc_max_mb
typeset test_time=8    # Measurement window: 4MB/s × 8s = ~32MB per device

# Disable DWPD and depth cap to test pure parallel throughput
log_must set_tunable32 L2ARC_DWPD_LIMIT 0
log_must set_tunable64 L2ARC_EXT_HEADROOM_PCT 0

# Set L2ARC_WRITE_MAX to 4MB/s to test parallel scaling
log_must set_tunable32 L2ARC_WRITE_MAX $((4 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure arc_max so L2ARC >= arc_c_max * 2 threshold for persistent markers
log_must set_tunable64 ARC_MAX $((arc_max_mb * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((200 * 1024 * 1024))

#
# Attach the cache devices named in $@ and leave the bytes L2ARC wrote
# over the measurement window in $measured.  Every phase gets devices of
# its own, so that none of them is ever seen with an L2ARC header on it
# and rebuilt instead of filled.
#
function measure_writes
{
	typeset devs="$*"
	typeset dev

	for dev in $devs; do
		log_must truncate -s ${cache_sz}M $dev
	done
	log_must zpool add -f $TESTPOOL cache $devs

	# Wait for L2ARC to start writing
	typeset l2_size=0
	for i in {1..30}; do
		l2_size=$(kstat arcstats.l2_size)
		[[ $l2_size -gt 0 ]] && break
		sleep 1
	done
	if [[ $l2_size -eq 0 ]]; then
		log_fail "L2ARC did not start writing ($devs)"
	fi

	typeset start=$(kstat arcstats.l2_write_bytes)
	log_must sleep $test_time
	typeset end=$(kstat arcstats.l2_write_bytes)

	#
	# Detaching the cache devices drops the L2ARC headers of everything
	# they hold, so the very same buffers are candidates again for the
	# next phase and the ARC does not have to be refilled.
	#
	log_must zpool remove $TESTPOOL $devs
	log_must rm -f $devs

	measured=$((end - start))
}

typeset measured=0

log_must truncate -s 5G $VDEV
log_must zpool create -f $TESTPOOL $VDEV

# Fill the ARC so the eviction lists have stable evictable buffers.
log_must file_write -o create -f /$TESTPOOL/file -b 1048576 -c $fill_mb -d R
log_must zpool sync $TESTPOOL

measure_writes $VDIR/e
typeset single_writes=$measured
typeset single_expected=$((4 * 1024 * 1024 * test_time))
log_note "Single-device writes: $((single_writes / 1024 / 1024))MB" \
    "(expected ~$((single_expected / 1024 / 1024))MB)"

measure_writes $VDIR/f $VDIR/g
typeset dual_writes=$measured
typeset dual_expected=$((4 * 1024 * 1024 * 2 * test_time))
log_note "Dual-device writes: $((dual_writes / 1024 / 1024))MB" \
    "(expected ~$((dual_expected / 1024 / 1024))MB)"

# Verify each phase reached at least 80% of its rate limit, and that the
# second device did add to the throughput rather than just share it.
typeset single_min=$((single_expected * 80 / 100))
typeset dual_min=$((dual_expected * 80 / 100))
typeset scale_min=$((single_writes * 150 / 100))

if [[ $single_writes -lt $single_min ]]; then
	log_fail "Single-device writes $((single_writes / 1024 / 1024))MB" \
	    "below minimum $((single_min / 1024 / 1024))MB"
fi
if [[ $dual_writes -lt $dual_min ]]; then
	log_fail "Dual-device writes $((dual_writes / 1024 / 1024))MB" \
	    "below minimum $((dual_min / 1024 / 1024))MB"
fi
if [[ $dual_writes -lt $scale_min ]]; then
	log_fail "Dual-device writes $((dual_writes / 1024 / 1024))MB" \
	    "did not scale over the single device" \
	    "($((single_writes / 1024 / 1024))MB)"
fi

destroy_pool $TESTPOOL

log_pass "L2ARC parallel writes scale with number of cache devices."
