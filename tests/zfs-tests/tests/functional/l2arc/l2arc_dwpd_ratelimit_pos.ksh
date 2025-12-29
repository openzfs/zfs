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
#	2. Create pool with cache device (L2ARC >= arc_c_max * 2).
#	3. Populate L2ARC to complete first pass - DWPD only limits writes
#	   after first pass, so we must fill L2ARC first.
#	4. Delete the file to free ARC and invalidate L2ARC entries.
#	5. Write fresh data - now DWPD rate limiting controls refill rate.
#	6. Measure L2ARC writes over test period.
#	7. Repeat 1-6 for DWPD values 0, 10000, 5000, 1800.
#	8. Verify DWPD=0 > DWPD=10000 > DWPD=5000 > DWPD=1800.
#

verify_runnable "global"

log_assert "L2ARC DWPD rate limiting correctly limits write rate."

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
typeset cache_sz=900
typeset fill_mb=1500
typeset test_time=15

# Configure arc_max = 400MB so L2ARC (900MB) >= arc_c_max * 2 threshold
log_must set_tunable64 ARC_MAX $((400 * 1024 * 1024))
log_must set_tunable64 ARC_MIN $((200 * 1024 * 1024))
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_WRITE_MAX $((200 * 1024 * 1024))

# Create larger main vdev to accommodate fill data
log_must truncate -s 5G $VDEV
log_must truncate -s ${cache_sz}M $VDEV_CACHE

typeset -A results

# Test each DWPD value with fresh pool.
# Minimum DWPD=1800 (18 DWPD) gives ~192KB/s (900MB*18/86400), enough to
# write one block (128KB) + log overhead (64KB) without accumulating budget.
for dwpd in 0 10000 5000 1800; do
	log_must set_tunable32 L2ARC_DWPD_LIMIT $dwpd

	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
	log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

	# Populate L2ARC in chunks to complete first pass
	# (DWPD only limits after first pass)
	log_must dd if=/dev/urandom of=/$TESTPOOL/fill1 bs=1M count=$((fill_mb/3))
	log_must sleep 5
	log_must dd if=/dev/urandom of=/$TESTPOOL/fill2 bs=1M count=$((fill_mb/3))
	log_must sleep 5
	log_must dd if=/dev/urandom of=/$TESTPOOL/fill3 bs=1M count=$((fill_mb/3))
	log_must sleep 5

	# Delete files to free ARC and invalidate L2ARC entries
	log_must rm /$TESTPOOL/fill1 /$TESTPOOL/fill2 /$TESTPOOL/fill3
	log_must sync

	# Take baseline - L2ARC now has space for fresh data
	baseline=$(kstat arcstats.l2_write_bytes)
	log_note "Baseline for DWPD=$dwpd: ${baseline}"

	# Write fresh data - DWPD rate limiting now controls refill rate
	# Write arc_max worth of data since that's what flows through ARC
	dd if=/dev/urandom of=/$TESTPOOL/test bs=1M count=400 &
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
if [[ ${results[10000]} -le ${results[5000]} ]]; then
	log_fail "DWPD=10000 should write more than DWPD=5000"
fi
if [[ ${results[5000]} -le ${results[1800]} ]]; then
	log_fail "DWPD=5000 should write more than DWPD=1800"
fi

log_must zpool destroy $TESTPOOL

log_pass "L2ARC DWPD rate limiting correctly limits write rate."
