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
#	2. Create pool with cache device (L2ARC >= arc_c_max * 2).
#	3. Fill L2ARC with staged writes and wait for first pass to complete.
#	4. Measure DWPD-limited writes with continuous workload.
#	5. Export and import pool.
#	6. Wait for rebuild, then fill L2ARC with staged writes.
#	7. Measure DWPD-limited writes again.
#	8. Verify rate limiting still works after import (non-zero writes).
#

verify_runnable "global"

log_assert "L2ARC DWPD rate limiting works after pool export/import."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	restore_tunable L2ARC_WRITE_MAX
	restore_tunable L2ARC_NOPREFETCH
	restore_tunable L2ARC_DWPD_LIMIT
	restore_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
	restore_tunable ARC_MIN
	restore_tunable ARC_MAX
}
log_onexit cleanup

# Save original tunables
save_tunable L2ARC_WRITE_MAX
save_tunable L2ARC_NOPREFETCH
save_tunable L2ARC_DWPD_LIMIT
save_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE
save_tunable ARC_MIN
save_tunable ARC_MAX

# Test parameters
typeset cache_sz=900
typeset fill_mb=1500
typeset test_time=15

# Set DWPD before pool creation (10000 = 100 DWPD)
log_must set_tunable32 L2ARC_DWPD_LIMIT 10000
log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

# Configure arc_max = 400MB so L2ARC (900MB) >= arc_c_max * 2 threshold
log_must set_tunable64 ARC_MAX $((400 * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((200 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_WRITE_MAX $((200 * 1024 * 1024))

# Create larger main vdev to accommodate fill data
log_must truncate -s 8G $VDEV
log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

# Staged fills to allow L2ARC to drain between writes
log_must dd if=/dev/urandom of=/$TESTPOOL/file1a bs=1M count=$((fill_mb/3))
log_must sleep 5
log_must dd if=/dev/urandom of=/$TESTPOOL/file1b bs=1M count=$((fill_mb/3))
log_must sleep 5
log_must dd if=/dev/urandom of=/$TESTPOOL/file1c bs=1M count=$((fill_mb/3))
log_must sleep 5

# Verify L2ARC is populated before export
typeset l2_size_before=$(kstat arcstats.l2_size)
log_note "L2ARC size before export: $((l2_size_before / 1024 / 1024))MB"
if [[ $l2_size_before -eq 0 ]]; then
	log_fail "L2ARC not populated before export"
fi

# Measure DWPD-limited writes before export
baseline1=$(kstat arcstats.l2_write_bytes)
log_note "Baseline before export: ${baseline1}"
dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1M count=2000 >/dev/null 2>&1 &
dd_pid=$!
log_must sleep $test_time
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
log_must sleep 2
end1=$(kstat arcstats.l2_write_bytes)
typeset writes_before=$((end1 - baseline1))

log_note "Writes before export: $((writes_before / 1024))KB"

# Verify L2ARC actually wrote data
if [[ $writes_before -eq 0 ]]; then
	log_fail "No L2ARC writes before export - DWPD may be too restrictive"
fi

# Export and import pool
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL

# Wait for rebuild to complete
log_must sleep 5

# Verify L2ARC is populated after import
typeset l2_size_after=$(kstat arcstats.l2_size)
log_note "L2ARC size after import: $((l2_size_after / 1024 / 1024))MB"
if [[ $l2_size_after -eq 0 ]]; then
	log_fail "L2ARC not populated after import"
fi

# Staged fills again after import
log_must dd if=/dev/urandom of=/$TESTPOOL/file3a bs=1M count=$((fill_mb/3))
log_must sleep 5
log_must dd if=/dev/urandom of=/$TESTPOOL/file3b bs=1M count=$((fill_mb/3))
log_must sleep 5
log_must dd if=/dev/urandom of=/$TESTPOOL/file3c bs=1M count=$((fill_mb/3))
log_must sleep 5

# Verify L2ARC is still populated after refill
l2_size=$(kstat arcstats.l2_size)
log_note "L2ARC size after refill: $((l2_size / 1024 / 1024))MB"

# Measure DWPD-limited writes after import
baseline2=$(kstat arcstats.l2_write_bytes)
log_note "Baseline after import: ${baseline2}"
dd if=/dev/urandom of=/$TESTPOOL/file4 bs=1M count=2000 >/dev/null 2>&1 &
dd_pid=$!
log_must sleep $test_time
kill $dd_pid 2>/dev/null
wait $dd_pid 2>/dev/null
log_must sleep 2
end2=$(kstat arcstats.l2_write_bytes)
typeset writes_after=$((end2 - baseline2))

log_note "Writes after import: $((writes_after / 1024))KB"

# Verify rate limiting persists after import
if [[ $writes_after -eq 0 ]]; then
	log_fail "No writes after import - rate limiting may be broken"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC DWPD rate limiting works after pool export/import."
