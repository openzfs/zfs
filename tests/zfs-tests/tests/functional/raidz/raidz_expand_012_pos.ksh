#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2026 by Skountz. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify the raidz_expansion_accounting feature flag and that
#	born/free space accounting remains self-consistent across expansion.
#
#	When a block is allocated ("born") and later freed ("killed"), both
#	operations must use the same deflation ratio.  The feature flag
#	records the txg at which per-block ratio tracking was enabled so
#	that blocks born before the feature use the legacy fixed ratio and
#	blocks born after use the per-birth-txg ratio.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool with 3 disks
#	2. Verify raidz_expansion_accounting feature is enabled (not active)
#	3. Write data (blocks born under original geometry)
#	4. Expand to 4 disks
#	5. Verify the feature becomes active after expansion
#	6. Verify the feature survives export/import
#	7. Write new data (blocks born under new geometry, after feature txg)
#	8. Delete ALL data (frees blocks from both geometries)
#	9. Verify pool accounting returns to near-empty (no born/free leak)
#	10. Write and delete several rounds to stress the accounting
#	11. Verify pool is consistent throughout
#	12. Test Scenario B: enable feature after expansion already happened
#

typeset -r devs=5
typeset -r dev_size_mb=512

typeset -a disks

prefetch_disable=$(get_tunable PREFETCH_DISABLE)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done

	log_must set_tunable32 PREFETCH_DISABLE $prefetch_disable
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1

# Create disk files
for i in {0..$(($devs))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

pool=$TESTPOOL
opts="-o cachefile=none"

#
# Helper: verify that alloc + free ≈ size within a given tolerance percent
#
function verify_accounting # <label> <tolerance_pct>
{
	typeset label=$1
	typeset tol_pct=${2:-5}
	typeset size=$(zpool list -Hpo size $pool)
	typeset alloc=$(zpool list -Hpo allocated $pool)
	typeset free=$(zpool list -Hpo free $pool)
	typeset sum=$(($alloc + $free))
	typeset diff

	if [[ $sum -gt $size ]]; then
		diff=$(($sum - $size))
	else
		diff=$(($size - $sum))
	fi

	typeset tolerance=$(($size * $tol_pct / 100))

	log_note "$label: size=$size alloc=$alloc free=$free" \
	    "(diff=$diff tolerance=$tolerance)"

	if [[ $diff -gt $tolerance ]]; then
		log_fail "$label: alloc+free differs from size by $diff" \
		    "(tolerance: $tolerance)"
	fi
}

#
# Helper: verify allocated space is below a threshold (bytes).
# Used to detect born/free accounting leaks after deleting all data.
#
function verify_near_empty # <label> <max_alloc_pct_of_size>
{
	typeset label=$1
	typeset max_pct=${2:-10}
	typeset size=$(zpool list -Hpo size $pool)
	typeset alloc=$(zpool list -Hpo allocated $pool)
	typeset threshold=$(($size * $max_pct / 100))

	log_note "$label: alloc=$alloc threshold=$threshold (${max_pct}% of size)"

	if [[ $alloc -gt $threshold ]]; then
		log_fail "$label: allocated space $alloc exceeds ${max_pct}%" \
		    "of pool size $size (threshold=$threshold)." \
		    "Possible born/free accounting leak."
	fi
}

# ---- Step 1: Create RAIDZ1 with 3 disks ----
log_must zpool create -f $opts $pool raidz1 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool

# ---- Step 2: Feature should be enabled but not active yet ----
typeset feat_status
feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
log_note "Feature status before expansion: $feat_status"

if [[ "$feat_status" != "enabled" ]]; then
	log_fail "raidz_expansion_accounting should be enabled on new pool" \
	    "(got: $feat_status)"
fi

# ---- Step 3: Write data under original geometry (pre-expansion blocks) ----
log_must zfs create $pool/fs
log_must dd if=/dev/urandom of=/$pool/fs/pre_expand bs=1M count=64
log_must sync_pool $pool
verify_accounting "Pre-expansion" 5

# Record baseline allocation after initial write
typeset baseline_alloc=$(zpool list -Hpo allocated $pool)
log_note "Baseline alloc after initial write: $baseline_alloc"

# ---- Step 4: Expand to 4 disks ----
log_must zpool attach -w $pool raidz1-0 ${disks[4]}
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

# ---- Step 5: Feature should now be active ----
feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
log_note "Feature status after expansion: $feat_status"

if [[ "$feat_status" != "active" ]]; then
	log_fail "raidz_expansion_accounting should be active after expansion" \
	    "(got: $feat_status)"
fi

verify_accounting "Post-expansion" 5

# ---- Step 6: Feature survives export/import ----
log_must zpool export $pool
log_must zpool import -d $TEST_BASE_DIR $pool

feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
log_note "Feature status after reimport: $feat_status"

if [[ "$feat_status" != "active" ]]; then
	log_fail "raidz_expansion_accounting not active after reimport" \
	    "(got: $feat_status)"
fi

verify_accounting "After reimport" 5

# ---- Step 7: Write data under new geometry (post-expansion blocks) ----
log_must dd if=/dev/urandom of=/$pool/fs/post_expand bs=1M count=64
log_must sync_pool $pool

verify_accounting "After post-expand write" 5

# ---- Step 8: Delete ALL data and verify no accounting leak ----
#
# This is the critical born/free test.  We wrote blocks under the old
# geometry and blocks under the new geometry.  When freed, the deflation
# ratio used for each block must match the ratio used when it was born.
# If there's a mismatch, the allocated space won't return to baseline.
#
log_must rm -f /$pool/fs/pre_expand /$pool/fs/post_expand
log_must sync_pool $pool
# Give a few txgs for deferred frees to settle
sleep 5
log_must sync_pool $pool

verify_accounting "After delete all" 5
verify_near_empty "After delete all" 10

# Verify per-dataset accounting: referenced should return to near-zero.
# This exercises bp_get_dsize_sync() with birth_txg -- if the born and
# kill deflation ratios disagree, ds_referenced_bytes would drift.
typeset ds_ref=$(zfs get -Hpo value referenced $pool/fs)
typeset ds_size=$(zpool list -Hpo size $pool)
typeset ds_ref_threshold=$(($ds_size * 2 / 100))
log_note "After delete all: ds referenced=$ds_ref threshold=$ds_ref_threshold"

if [[ $ds_ref -gt $ds_ref_threshold ]]; then
	log_fail "Dataset referenced ($ds_ref) still high after deleting" \
	    "all data (threshold=$ds_ref_threshold)." \
	    "Possible born/free deflation ratio mismatch."
fi

# ---- Step 9: Second expansion to 5 disks ----
log_must zpool attach -w $pool raidz1-0 ${disks[5]}
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

verify_accounting "After second expansion" 5

# Feature should still be active (stays active across expansions)
feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
if [[ "$feat_status" != "active" ]]; then
	log_fail "Feature should remain active after second expansion" \
	    "(got: $feat_status)"
fi

# ---- Step 10: Stress test - multiple write/delete cycles ----
#
# Exercise the accounting across multiple rounds, writing and deleting
# with the latest geometry to ensure no cumulative drift.
#
for round in 1 2 3; do
	log_must dd if=/dev/urandom of=/$pool/fs/stress_$round \
	    bs=1M count=48
done
log_must sync_pool $pool
verify_accounting "After stress writes" 10

for round in 1 2 3; do
	log_must rm -f /$pool/fs/stress_$round
done
log_must sync_pool $pool
sleep 5
log_must sync_pool $pool

verify_accounting "After stress deletes" 5
verify_near_empty "After stress deletes" 10

# Per-dataset born/free check after stress cycles
typeset ds_ref_stress=$(zfs get -Hpo value referenced $pool/fs)
log_note "After stress deletes: ds referenced=$ds_ref_stress"

if [[ $ds_ref_stress -gt $ds_ref_threshold ]]; then
	log_fail "Dataset referenced ($ds_ref_stress) still high after" \
	    "stress deletes (threshold=$ds_ref_threshold)." \
	    "Possible cumulative born/free drift."
fi

# ---- Step 11: Final pool integrity ----
verify_pool $pool

# ---- Step 12: Scenario B - enable feature after expansion ----
#
# Create a fresh pool with the feature disabled, expand it, then enable
# the feature.  It should activate immediately since expansion already
# happened.
#
log_must zpool destroy "$pool"
log_must zpool create -f $opts \
    -o feature@raidz_expansion_accounting=disabled \
    $pool raidz1 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool

feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
if [[ "$feat_status" != "disabled" ]]; then
	log_fail "Feature should be disabled (got: $feat_status)"
fi

# Expand without the feature enabled
log_must zpool attach -w $pool raidz1-0 ${disks[4]}
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
if [[ "$feat_status" != "disabled" ]]; then
	log_fail "Feature should remain disabled after expansion" \
	    "(got: $feat_status)"
fi

# Now enable the feature; it should activate immediately
log_must zpool set feature@raidz_expansion_accounting=enabled $pool

feat_status=$(zpool list -H -o feature@raidz_expansion_accounting $pool)
log_note "Feature status after post-expansion enable: $feat_status"
if [[ "$feat_status" != "active" ]]; then
	log_fail "Feature should activate immediately when enabled" \
	    "after expansion (got: $feat_status)"
fi

verify_accounting "Scenario B post-enable" 5

log_pass "RAIDZ expansion accounting feature flag and born/free consistency verified."
