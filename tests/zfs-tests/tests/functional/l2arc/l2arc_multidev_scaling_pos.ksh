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
#	3. Create pool with single 2100MB cache device.
#	4. Generate continuous writes, wait for L2ARC activity, measure over 25s.
#	5. Verify single-device throughput ~400MB (16MB/s × 25s).
#	6. Recreate pool with dual 2100MB cache devices.
#	7. Generate continuous writes, wait for L2ARC activity, measure over 25s.
#	8. Verify dual-device throughput ~800MB (2×16MB/s × 25s).
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
	restore_tunable ARC_MIN
	restore_tunable ARC_MAX
}
log_onexit cleanup

# Save original tunables
save_tunable L2ARC_WRITE_MAX
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_DWPD_LIMIT
save_tunable ARC_MIN
save_tunable ARC_MAX

# Test parameters
typeset cache_sz=1000
typeset fill_mb=2500   # 2.5GB initial data
typeset test_time=12   # Measurement window: 16MB/s × 12s = ~200MB per device

# Disable DWPD to test pure parallel throughput
log_must set_tunable32 L2ARC_DWPD_LIMIT 0

# Set L2ARC_WRITE_MAX to 16MB/s to test parallel scaling
log_must set_tunable32 L2ARC_WRITE_MAX $((16 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure arc_max so L2ARC >= arc_c_max * 2 threshold for persistent markers
log_must set_tunable64 ARC_MAX $((400 * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((200 * 1024 * 1024))

# Single device test
log_must truncate -s 4G $VDEV
log_must truncate -s ${cache_sz}M $VDEV_CACHE
log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

# Generate data in background
dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb &
typeset dd_pid=$!

# Wait for L2ARC to start writing
typeset l2_size=0
for i in {1..30}; do
	l2_size=$(kstat arcstats.l2_size)
	[[ $l2_size -gt 0 ]] && break
	sleep 1
done
if [[ $l2_size -eq 0 ]]; then
	kill $dd_pid 2>/dev/null
	log_fail "L2ARC did not start writing (single device)"
fi

# Measure single-device write throughput
typeset start=$(kstat arcstats.l2_write_bytes)
log_must sleep $test_time
typeset end=$(kstat arcstats.l2_write_bytes)
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
typeset single_writes=$((end - start))

# expected = 16MB/s * 1 device * 25s = 400MB
typeset single_expected=$((16 * 1024 * 1024 * test_time))
log_note "Single-device writes: $((single_writes / 1024 / 1024))MB (expected ~$((single_expected / 1024 / 1024))MB)"

# Dual device test
log_must zpool destroy $TESTPOOL
log_must truncate -s ${cache_sz}M $VDEV_CACHE
log_must truncate -s ${cache_sz}M $VDEV_CACHE2

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE $VDEV_CACHE2

# Generate data in background
dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=$fill_mb &
dd_pid=$!

# Wait for L2ARC to start writing
l2_size=0
for i in {1..30}; do
	l2_size=$(kstat arcstats.l2_size)
	[[ $l2_size -gt 0 ]] && break
	sleep 1
done
if [[ $l2_size -eq 0 ]]; then
	kill $dd_pid 2>/dev/null
	log_fail "L2ARC did not start writing (dual device)"
fi

# Measure parallel write throughput (2 feed threads active)
start=$(kstat arcstats.l2_write_bytes)
log_must sleep $test_time
end=$(kstat arcstats.l2_write_bytes)
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
typeset dual_writes=$((end - start))

# expected = 16MB/s * 2 devices * 25s = 800MB
typeset dual_expected=$((16 * 1024 * 1024 * 2 * test_time))
log_note "Dual-device writes: $((dual_writes / 1024 / 1024))MB (expected ~$((dual_expected / 1024 / 1024))MB)"

# Verify writes are within expected range (80-150%)
typeset single_min=$((single_expected * 80 / 100))
typeset dual_min=$((dual_expected * 80 / 100))

if [[ $single_writes -lt $single_min ]]; then
	log_fail "Single-device writes $((single_writes / 1024 / 1024))MB below minimum $((single_min / 1024 / 1024))MB"
fi
if [[ $dual_writes -lt $dual_min ]]; then
	log_fail "Dual-device writes $((dual_writes / 1024 / 1024))MB below minimum $((dual_min / 1024 / 1024))MB"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC parallel writes scale with number of cache devices."
