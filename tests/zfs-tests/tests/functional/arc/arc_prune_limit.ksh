#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2024, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs_arc_prune_min_interval_ms rate limits arc_prune_async dispatch.
#
# STRATEGY:
# 1. Set zfs_arc_max to a small value to force memory pressure.
# 2. Set zfs_arc_prune_min_interval_ms to a measurable interval (e.g., 2000ms).
# 3. Generate a workload that creates many dnodes (files) to trigger metadata pressure.
# 4. Measure the increase in 'prune' arcstat over a fixed duration.
# 5. Assert that the number of prune dispatches is within a reasonable limit 
#    based on the interval.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 ARC_MAX $ORIG_ARC_MAX
	log_must set_tunable32 ARC_PRUNE_MIN_INTERVAL_MS $ORIG_PRUNE_INTERVAL
	default_cleanup
}

log_onexit cleanup

# Save original values
ORIG_ARC_MAX=$(get_tunable ARC_MAX)
ORIG_PRUNE_INTERVAL=$(get_tunable ARC_PRUNE_MIN_INTERVAL_MS)

# Set tunables
# Set ARC MAX to 64MB to force frequent eviction
log_must set_tunable64 ARC_MAX 67108864
# Set prune interval to 1 second (1000ms)
log_must set_tunable32 ARC_PRUNE_MIN_INTERVAL_MS 1000

# Create a filesystem for testing
default_setup $DISKS

# Get initial prune count
PRUNE_START=$(get_arcstat prune)

# Run workload for 10 seconds
# We create many files to pin dnodes and force eviction pressure
log_note "Generating load..."
start_time=$(current_epoch)
end_time=$((start_time + 10))

while [[ $(current_epoch) -lt $end_time ]]; do
    # Create a batch of files
    for i in {1..100}; do
        touch $TESTDIR/file.$(openssl rand -hex 4)
    done
    # Trigger sync to flush changes and potentially trigger arc reclaim
    sync
done

PRUNE_END=$(get_arcstat prune)
PRUNE_DIFF=$((PRUNE_END - PRUNE_START))
DURATION=$(( $(current_epoch) - start_time ))

log_note "Prune count start: $PRUNE_START"
log_note "Prune count end: $PRUNE_END"
log_note "Prune count diff: $PRUNE_DIFF"
log_note "Duration: $DURATION seconds"

# We expect at most roughly 1 dispatch per second per filesystem.
# We have at least 1 filesystem ($TESTDIR).
# Allow for some variance, but if it's spinning, it would be thousands.
# With 1000ms interval, in 10 seconds, we expect roughly 10-20 prunes max per FS.
# Let's be generous and say < 100 is "rate limited" vs > 1000 which would be "spinning".

if [[ $PRUNE_DIFF -gt 200 ]]; then
    log_fail "Excessive arc_prune dispatches detected: $PRUNE_DIFF in $DURATION seconds. Rate limiting may be failing."
else
    log_pass "arc_prune dispatches within expected limits ($PRUNE_DIFF)."
fi
