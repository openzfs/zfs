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
#	L2ARC DWPD rate limiting works after pool export/import.
#
# STRATEGY:
#	1. Set DWPD limit before creating pool.
#	2. Create pool with cache device.
#	3. Fill L2ARC to complete first pass.
#	4. Measure writes with rate limiting active.
#	5. Export and import pool.
#	6. Measure writes again.
#	7. Verify rate limiting still works after import.
#

verify_runnable "global"

log_assert "L2ARC DWPD rate limiting works after pool export/import."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_WRITE_MAX $write_max
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_DWPD_LIMIT $dwpd_limit
	log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE $rebuild_min
	log_must set_tunable64 ARC_MIN $arc_min
	log_must set_tunable64 ARC_MAX $arc_max
}
log_onexit cleanup

# Save original tunables
typeset write_max=$(get_tunable L2ARC_WRITE_MAX)
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
typeset dwpd_limit=$(get_tunable L2ARC_DWPD_LIMIT)
typeset rebuild_min=$(get_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE)
typeset arc_min=$(get_tunable ARC_MIN)
typeset arc_max=$(get_tunable ARC_MAX)

# Test parameters (total writes must fit in 1GB vdev)
typeset cache_sz=400
typeset fill_mb=300
typeset test_time=10

# Set DWPD before pool creation
log_must set_tunable32 L2ARC_DWPD_LIMIT 100
log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

# Configure arc_max = 1.5 * cache_size
log_must set_tunable64 ARC_MIN $((cache_sz * 3 / 4 * 1024 * 1024))
log_must set_tunable64 ARC_MAX $((cache_sz * 3 / 2 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_WRITE_MAX $((100 * 1024 * 1024))

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

# Fill first pass
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1M count=$fill_mb
log_must sleep 6

# Measure writes before export
typeset start1=$(kstat arcstats.l2_write_bytes)
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=100
log_must sleep $test_time
typeset end1=$(kstat arcstats.l2_write_bytes)
typeset writes_before=$((end1 - start1))

log_note "Writes before export: $((writes_before / 1024))KB"

# Export and import pool
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL

# Wait for rebuild to complete
arcstat_quiescence_noecho l2_size

# Fill again to complete first pass after import
log_must dd if=/dev/urandom of=/$TESTPOOL/file3 bs=1M count=$fill_mb
log_must sleep 6

# Measure writes after import
typeset start2=$(kstat arcstats.l2_write_bytes)
log_must dd if=/dev/urandom of=/$TESTPOOL/file4 bs=1M count=100
log_must sleep $test_time
typeset end2=$(kstat arcstats.l2_write_bytes)
typeset writes_after=$((end2 - start2))

log_note "Writes after import: $((writes_after / 1024))KB"

# Verify rate limiting works after import (writes should be similar)
# Allow 3x variance due to timing differences
if [[ $writes_after -eq 0 ]]; then
	log_fail "No writes after import - rate limiting may be broken"
fi

if [[ $writes_after -gt $((writes_before * 3)) ]]; then
	log_fail "Writes after import too high - rate limiting may not be active"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC DWPD rate limiting works after pool export/import."
