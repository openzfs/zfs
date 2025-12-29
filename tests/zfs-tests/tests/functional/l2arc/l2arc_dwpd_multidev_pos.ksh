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
#	L2ARC DWPD rate limiting works independently per device.
#
# STRATEGY:
#	1. Set DWPD limit before creating pool.
#	2. Create pool with two cache devices, measure total writes.
#	3. Recreate pool with one cache device, measure total writes.
#	4. Verify dual-device writes exceed single-device writes.
#

verify_runnable "global"

log_assert "L2ARC DWPD rate limiting works independently per device."

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
typeset cache_sz=200
typeset fill_mb=500
typeset test_time=10
typeset VDEV_CACHE2="$VDIR/cache2"

# Set DWPD before pool creation
log_must set_tunable32 L2ARC_DWPD_LIMIT 100

# Configure arc_max = 1.5 * total_cache_size
log_must set_tunable64 ARC_MIN $((cache_sz * 2 * 3 / 4 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((cache_sz * 2 * 3 / 2 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_WRITE_MAX $((100 * 1024 * 1024))

# Create two cache devices
log_must truncate -s ${cache_sz}M $VDEV_CACHE
log_must truncate -s ${cache_sz}M $VDEV_CACHE2

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE $VDEV_CACHE2

# Fill first pass on both devices
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb
log_must sleep 6

# Measure writes with two devices
typeset start=$(kstat arcstats.l2_write_bytes)
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=100
log_must sleep $test_time
typeset end=$(kstat arcstats.l2_write_bytes)
typeset dual_writes=$((end - start))

log_note "Dual-device writes: $((dual_writes / 1024))KB"

# Recreate with single device for comparison
# Adjust arc_max so persist_threshold < single cache size
log_must zpool destroy $TESTPOOL
log_must set_tunable64 ARC_MIN $((cache_sz * 3 / 4 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((cache_sz * 3 / 2 * 1024 * 1024))
log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

# Fill first pass
log_must dd if=/dev/urandom of=/$TESTPOOL/file3 bs=1M count=$fill_mb
log_must sleep 6

# Measure writes with single device
start=$(kstat arcstats.l2_write_bytes)
log_must dd if=/dev/urandom of=/$TESTPOOL/file4 bs=1M count=100
log_must sleep $test_time
end=$(kstat arcstats.l2_write_bytes)
typeset single_writes=$((end - start))

log_note "Single-device writes: $((single_writes / 1024))KB"

# Dual devices should write more than single (each has own DWPD budget)
if [[ $dual_writes -le $single_writes ]]; then
	log_fail "Dual-device ($dual_writes) should exceed" \
	    "single-device ($single_writes)"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC DWPD rate limiting works independently per device."
