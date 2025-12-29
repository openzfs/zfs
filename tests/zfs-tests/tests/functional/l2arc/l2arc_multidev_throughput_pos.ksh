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
#	2. Create pool with 5 cache devices.
#	3. Write data and measure L2ARC throughput.
#	4. Verify throughput scales with device count (~32MB/s per device).
#

verify_runnable "global"

log_assert "L2ARC parallel writes scale with number of cache devices."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_WRITE_MAX $write_max
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_DWPD_LIMIT $dwpd_limit
	log_must set_tunable64 ARC_MIN $arc_min
	log_must set_tunable64 ARC_MAX $arc_max
}
log_onexit cleanup

# Save original tunables
typeset write_max=$(get_tunable L2ARC_WRITE_MAX)
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
typeset dwpd_limit=$(get_tunable L2ARC_DWPD_LIMIT)
typeset arc_min=$(get_tunable ARC_MIN)
typeset arc_max=$(get_tunable ARC_MAX)

# Test parameters
typeset num_devs=5
typeset cache_sz=200
typeset test_time=5
typeset expected_rate=$((32 * 1024 * 1024))  # 32 MB/s per device

# Disable DWPD rate limiting
log_must set_tunable32 L2ARC_DWPD_LIMIT 0

# Use default L2ARC_WRITE_MAX (32MB/s per device)
log_must set_tunable32 L2ARC_WRITE_MAX $expected_rate
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Configure arc_max large enough
log_must set_tunable64 ARC_MIN $((512 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((1024 * 1024 * 1024))

# Create cache devices
typeset cache_devs=""
for i in $(seq 1 $num_devs); do
	typeset dev="$VDIR/cache$i"
	log_must truncate -s ${cache_sz}M $dev
	cache_devs="$cache_devs $dev"
done

log_must zpool create -f $TESTPOOL $VDEV cache $cache_devs

# Generate data and measure L2ARC writes
typeset start=$(kstat arcstats.l2_write_bytes)
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=800
log_must sleep $test_time
typeset end=$(kstat arcstats.l2_write_bytes)

typeset bytes=$((end - start))
typeset bytes_mb=$((bytes / 1024 / 1024))
# expected = 32MB/s * 5 devices * 5 seconds = 800MB
typeset expected=$((expected_rate * num_devs * test_time))
typeset expected_mb=$((expected / 1024 / 1024))

log_note "L2ARC writes: ${bytes_mb}MB (expected ~${expected_mb}MB)"

# Verify writes are at least 80% of expected
typeset min_bytes=$((expected * 80 / 100))
if [[ $bytes -lt $min_bytes ]]; then
	log_fail "Writes ${bytes_mb}MB below minimum $((min_bytes/1024/1024))MB"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC parallel writes scale with number of cache devices."
