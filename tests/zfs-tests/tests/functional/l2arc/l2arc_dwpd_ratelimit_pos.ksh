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
#	L2ARC DWPD rate limiting correctly limits write rate.
#
# STRATEGY:
#	1. Set DWPD limit before creating pool.
#	2. Create pool with cache device (arc_max = 1.5 * cache_size).
#	3. Fill L2ARC to complete first pass.
#	4. Measure writes over test period.
#	5. Repeat 1-4 for DWPD values 0, 100, 1000, 10000.
#	6. Verify DWPD=0 > DWPD=10000 > DWPD=1000 > DWPD=100.
#

verify_runnable "global"

log_assert "L2ARC DWPD rate limiting correctly limits write rate."

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
typeset cache_sz=900
typeset fill_mb=1200
typeset test_time=15

# Configure arc_max = 1.8 * cache_size for continuous L2ARC feed
log_must set_tunable64 ARC_MIN $((cache_sz * 8 / 10 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((cache_sz * 18 / 10 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_WRITE_MAX $((200 * 1024 * 1024))

# Create larger main vdev to accommodate fill data
log_must truncate -s 5G $VDEV
log_must truncate -s ${cache_sz}M $VDEV_CACHE

typeset -A results

# Test each DWPD value with fresh pool to measure first-pass fill
for dwpd in 0 10000 1000 100; do
	log_must set_tunable32 L2ARC_DWPD_LIMIT $dwpd

	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
	log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

	# Fill first pass and wait for L2ARC writes to stabilize
	log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb
	log_must sleep 10

	# Take baseline after first pass completes
	baseline=$(kstat arcstats.l2_write_bytes)
	log_note "Baseline for DWPD=$dwpd: ${baseline}"

	# Generate continuous workload to measure DWPD-limited L2ARC writes
	# Write 2GB to ensure continuous L2ARC feed pressure throughout measurement
	dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=2000 >/dev/null 2>&1 &
	dd_pid=$!
	log_must sleep $test_time
	kill $dd_pid 2>/dev/null
	wait $dd_pid 2>/dev/null
	log_must sleep 2
	end=$(kstat arcstats.l2_write_bytes)

	results[$dwpd]=$((end - baseline))
	log_note "DWPD=$dwpd: delta=$((results[$dwpd] / 1024))KB"
done

# Verify ordering: higher DWPD = more writes, 0 = unlimited
if [[ ${results[0]} -le ${results[10000]} ]]; then
	log_fail "DWPD=0 (unlimited) should write more than DWPD=10000"
fi
if [[ ${results[10000]} -le ${results[1000]} ]]; then
	log_fail "DWPD=10000 should write more than DWPD=1000"
fi
if [[ ${results[1000]} -le ${results[100]} ]]; then
	log_fail "DWPD=1000 should write more than DWPD=100"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC DWPD rate limiting correctly limits write rate."
