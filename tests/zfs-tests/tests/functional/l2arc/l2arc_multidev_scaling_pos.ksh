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
#	L2ARC parallel writes scale with number of cache devices.
#
# STRATEGY:
#	1. Configure L2ARC write rate to 16MB/s per device.
#	2. Disable DWPD rate limiting to test pure parallel throughput.
#	3. Create pool with single 900MB cache device.
#	4. Generate continuous writes and measure L2ARC throughput over 25s.
#	5. Recreate pool with dual 900MB cache devices (1800MB total).
#	6. Generate continuous writes and measure L2ARC throughput over 25s.
#	7. Verify dual-device throughput is ~2x single-device throughput,
#	   demonstrating that per-device feed threads enable parallel writes.
#	   Expected: single ~400MB (16MB/s), dual ~800MB (2×16MB/s).
#

verify_runnable "global"

log_assert "L2ARC parallel writes scale with number of cache devices."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_DWPD_LIMIT $dwpd_limit
	log_must set_tunable64 ARC_MIN $arc_min
	log_must set_tunable64 ARC_MAX $arc_max
}
log_onexit cleanup

# Save original tunables
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
typeset dwpd_limit=$(get_tunable L2ARC_DWPD_LIMIT)
typeset arc_min=$(get_tunable ARC_MIN)
typeset arc_max=$(get_tunable ARC_MAX)

# Test parameters
typeset cache_sz=900   # 900MB per device
typeset fill_mb=2500   # 2.5GB initial data
typeset test_time=25   # Measurement window: 16MB/s × 25s = 400MB per device
typeset VDEV_CACHE2="$VDIR/cache2"

# Disable DWPD to test pure parallel throughput
log_must set_tunable32 L2ARC_DWPD_LIMIT 0

# Set L2ARC_WRITE_MAX to 16MB/s to test parallel scaling
log_must set_tunable32 L2ARC_WRITE_MAX $((16 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure arc_max so persist threshold (arc_max/2) is below device size
# persist_threshold = 1024MB/2 = 512MB, device usable ~896MB > 512MB
log_must set_tunable64 ARC_MIN $((512 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((1024 * 1024 * 1024))

# Single device test: 1 × 900MB
log_must truncate -s ${cache_sz}M $VDEV_CACHE
log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

# Measure single-device write throughput
typeset start=$(kstat arcstats.l2_write_bytes)
dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb &
typeset dd_pid=$!
log_must sleep $test_time
typeset end=$(kstat arcstats.l2_write_bytes)
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
typeset single_writes=$((end - start))

log_note "Single-device writes: $((single_writes / 1024 / 1024))MB (target ~400MB at 16MB/s)"

# Dual device test: 2 × 900MB = 1800MB total capacity
log_must zpool destroy $TESTPOOL
log_must truncate -s ${cache_sz}M $VDEV_CACHE
log_must truncate -s ${cache_sz}M $VDEV_CACHE2

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE $VDEV_CACHE2

# Measure parallel write throughput (2 feed threads active)
start=$(kstat arcstats.l2_write_bytes)
dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=$fill_mb &
dd_pid=$!
log_must sleep $test_time
end=$(kstat arcstats.l2_write_bytes)
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
typeset dual_writes=$((end - start))

log_note "Dual-device writes: $((dual_writes / 1024 / 1024))MB (target ~800MB at 2×16MB/s)"

# Verify parallel write scaling (dual should be ~2x single)
# Actual values may be lower than target due to dd overhead, ARC pressure,
# and feed thread scheduling, but ratio should show clear parallel benefit.
# Require 1.5x minimum to pass.
typeset min_ratio=$((single_writes * 3 / 2))
if [[ $dual_writes -lt $min_ratio ]]; then
	log_fail "Dual-device writes ($((dual_writes / 1024 / 1024))MB)" \
	    "should be at least 1.5x single ($((single_writes / 1024 / 1024))MB)"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC parallel writes scale with number of cache devices."
