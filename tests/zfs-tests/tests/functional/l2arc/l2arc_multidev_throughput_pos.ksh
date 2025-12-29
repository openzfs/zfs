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
#	1. Disable DWPD rate limiting.
#	2. Create pool with 2 cache devices.
#	3. Write data and measure L2ARC throughput.
#	4. Verify throughput scales with device count (~16MB/s per device).
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
typeset num_devs=2
typeset cache_sz=1000  # 2000MB total > 1900MB (arc_max*2) threshold
typeset test_time=10
typeset fill_mb=1500
typeset expected_rate=$((32 * 1024 * 1024))  # 32 MB/s per device

# Disable DWPD rate limiting
log_must set_tunable32 L2ARC_DWPD_LIMIT 0

# Set L2ARC_WRITE_MAX to 32MB/s per device (64MB/s total with 2 devices)
log_must set_tunable32 L2ARC_WRITE_MAX $expected_rate
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure arc_max large enough to feed L2ARC
log_must set_tunable64 ARC_MAX $((950 * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((512 * 1024 * 1024))

# Create cache devices (using letters e-f to follow cfg naming convention)
typeset cache_devs=""
for letter in e f; do
	typeset dev="$VDIR/$letter"
	log_must truncate -s ${cache_sz}M $dev
	cache_devs="$cache_devs $dev"
done

log_must truncate -s 2G $VDEV
log_must zpool create -f $TESTPOOL $VDEV cache $cache_devs

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
	log_fail "L2ARC did not start writing"
fi

# Measure L2ARC throughput over test window
typeset start=$(kstat arcstats.l2_write_bytes)
log_must sleep $test_time
typeset end=$(kstat arcstats.l2_write_bytes)
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null

typeset bytes=$((end - start))
typeset bytes_mb=$((bytes / 1024 / 1024))
# expected = 32MB/s * 2 devices * 10 seconds = 640MB
typeset expected=$((expected_rate * num_devs * test_time))
typeset expected_mb=$((expected / 1024 / 1024))

log_note "L2ARC writes: ${bytes_mb}MB (expected ~${expected_mb}MB)"

# Verify writes are within expected range (75-150%)
typeset min_bytes=$((expected * 75 / 100))
typeset max_bytes=$((expected * 150 / 100))
if [[ $bytes -lt $min_bytes ]]; then
	log_fail "Writes ${bytes_mb}MB below minimum $((min_bytes/1024/1024))MB"
fi
if [[ $bytes -gt $max_bytes ]]; then
	log_fail "Writes ${bytes_mb}MB above maximum $((max_bytes/1024/1024))MB"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC parallel writes scale with number of cache devices."
