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
#	Verify that space accounting is correct after RAIDZ expansion.
#	After expanding a RAIDZ vdev, the reported pool capacity should
#	reflect the current (post-expansion) geometry, not the original.
#
# STRATEGY:
#	1. Create a RAIDZ2 pool with 4 disks
#	2. Write test data and record the pool size and free space
#	3. Attach a 5th disk to expand the RAIDZ2 vdev
#	4. Wait for expansion to complete
#	5. Verify that the reported pool capacity increased to reflect the
#	   new geometry (3 data disks instead of 2)
#	6. Verify that reported free space is consistent:
#	   free space should be approximately (new_capacity - data_used)
#	7. Verify zfs dataset available also reflects the correction
#	   (exercises spa_update_dspace() code path)
#	8. Verify accounting survives export/import
#

typeset -r devs=4
typeset -r dev_size_mb=768

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

# Create a RAIDZ2 pool with 3 disks (1 data + 2 parity)
log_must zpool create -f $opts $pool raidz2 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool

# Write test data
log_must zfs create $pool/fs
log_must dd if=/dev/urandom of=/$pool/fs/testfile bs=1M count=300
log_must sync_pool $pool

# Record pre-expansion accounting
typeset pre_size=$(zpool list -Hpo size $pool)
typeset pre_alloc=$(zpool list -Hpo allocated $pool)
typeset pre_free=$(zpool list -Hpo free $pool)
typeset pre_usable=$(zpool list -Hpo usable $pool)
typeset pre_avail=$(zfs get -Hpo value available $pool/fs)

log_note "Pre-expansion: size=$pre_size alloc=$pre_alloc free=$pre_free usable=$pre_usable avail=$pre_avail"

# Verify pre-expansion consistency: free should be approximately size - alloc
typeset pre_sum=$(($pre_alloc + $pre_free))
typeset pre_diff
if [[ $pre_sum -gt $pre_size ]]; then
	pre_diff=$(($pre_sum - $pre_size))
else
	pre_diff=$(($pre_size - $pre_sum))
fi
# Allow 5% tolerance for metadata overhead
typeset tolerance=$(($pre_size / 20))
if [[ $pre_diff -gt $tolerance ]]; then
	log_fail "Pre-expansion: alloc+free differs from size by $pre_diff" \
	    "(tolerance: $tolerance)"
fi

# Expand: attach 4th disk
log_must zpool attach -w $pool raidz2-0 ${disks[4]}

# Wait for scrub to complete if one was triggered
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

# Record post-expansion accounting
typeset post_size=$(zpool list -Hpo size $pool)
typeset post_alloc=$(zpool list -Hpo allocated $pool)
typeset post_free=$(zpool list -Hpo free $pool)
typeset post_usable=$(zpool list -Hpo usable $pool)
typeset post_avail=$(zfs get -Hpo value available $pool/fs)

log_note "Post-expansion: size=$post_size alloc=$post_alloc free=$post_free usable=$post_usable avail=$post_avail"

# Verify that raw pool size increased
if [[ $post_size -le $pre_size ]]; then
	log_fail "Pool size did not increase after expansion" \
	    "(before=$pre_size after=$post_size)"
fi

# Verify the deflation ratio correction via the USABLE property.
# USABLE reflects deflated (parity-adjusted) capacity; SIZE is raw physical.
#
# With 4 disks of ~768MB each: total raw ~ 3072MB
# Old geometry (3 disks RAIDZ2): usable ratio = 1/3 = 33%
# New geometry (4 disks RAIDZ2): usable ratio = 2/4 = 50%
# Expected new usable ~ 3072MB * 0.5 = 1536MB
# Old usable was      ~ 2304MB * 0.33 = 768MB (3 disks)
# Usable increase should be ~100%, well above the raw 33%.
#
# Use USABLE (not SIZE) because SIZE is raw physical space that grows
# linearly with the number of disks.  USABLE incorporates the deflation
# ratio correction and should reflect the improved data-to-parity ratio.
typeset usable_increase=$(( ($post_usable - $pre_usable) * 100 / $pre_usable ))
log_note "Usable capacity increase: ${usable_increase}%"

# With RAIDZ2 expanding from 3->4 disks:
# Raw space increases by 33% (1 disk / 3 disks)
# But usable space should increase by more than 33% because the
# data-to-parity ratio improves (from 1/3 to 2/4).  If the increase
# is only 33% or less, the deflation ratio is stuck at old geometry.
if [[ $usable_increase -le 33 ]]; then
	log_fail "Usable capacity increase too small (${usable_increase}%)." \
	    "Expected >33% for RAIDZ2 3->4 expansion." \
	    "This suggests deflation ratio is using old geometry."
fi

# Verify the spa_update_dspace() correction via zfs available.
# This exercises a different code path than ZPOOL_PROP_USABLE above:
# zfs available -> dsl_pool_adjustedsize() -> spa_get_dspace() ->
# spa_update_dspace() which applies the same deflation correction.
typeset avail_increase=$(( ($post_avail - $pre_avail) * 100 / $pre_avail ))
log_note "Dataset available increase: ${avail_increase}%"

if [[ $avail_increase -le 33 ]]; then
	log_fail "Dataset available increase too small (${avail_increase}%)." \
	    "Expected >33% for RAIDZ2 3->4 expansion." \
	    "This suggests spa_update_dspace() correction is not applied."
fi

# Verify post-expansion consistency: free should be approximately size - alloc
typeset post_sum=$(($post_alloc + $post_free))
typeset post_diff
if [[ $post_sum -gt $post_size ]]; then
	post_diff=$(($post_sum - $post_size))
else
	post_diff=$(($post_size - $post_sum))
fi
typeset post_tolerance=$(($post_size / 20))
if [[ $post_diff -gt $post_tolerance ]]; then
	log_fail "Post-expansion: alloc+free differs from size by $post_diff" \
	    "(tolerance: $post_tolerance)"
fi

# Verify accounting survives export/import (vdev_deflate_ratio_current
# is recomputed in vdev_set_deflate_ratio() during vdev_open())
log_must zpool export $pool
log_must zpool import -d $TEST_BASE_DIR $pool

typeset reimport_size=$(zpool list -Hpo size $pool)
typeset reimport_alloc=$(zpool list -Hpo allocated $pool)
typeset reimport_free=$(zpool list -Hpo free $pool)
typeset reimport_usable=$(zpool list -Hpo usable $pool)
typeset reimport_avail=$(zfs get -Hpo value available $pool/fs)

log_note "After reimport: size=$reimport_size alloc=$reimport_alloc free=$reimport_free usable=$reimport_usable avail=$reimport_avail"

if [[ $reimport_size -ne $post_size ]]; then
	log_fail "Pool size changed after reimport" \
	    "(before=$post_size after=$reimport_size)"
fi

# Verify that usable capacity survives export/import.
# vdev_deflate_ratio_current is not persisted on disk — it is recomputed
# in vdev_set_deflate_ratio() during vdev_open().  The correction in
# spa_prop_get_config() depends on this recomputed value and vs_alloc,
# which can shift slightly across export/import (deferred frees settle).
typeset usable_reimport_diff
if [[ $reimport_usable -gt $post_usable ]]; then
	usable_reimport_diff=$(($reimport_usable - $post_usable))
else
	usable_reimport_diff=$(($post_usable - $reimport_usable))
fi
typeset usable_reimport_tol=$(($post_usable / 100))
if [[ $usable_reimport_diff -gt $usable_reimport_tol ]]; then
	log_fail "Usable capacity changed significantly after reimport" \
	    "(before=$post_usable after=$reimport_usable" \
	    "diff=$usable_reimport_diff)"
fi

# Allow 1% tolerance for zfs available — it includes deferred frees
# and other transient state that can shift slightly across export/import.
typeset avail_reimport_diff
if [[ $reimport_avail -gt $post_avail ]]; then
	avail_reimport_diff=$(($reimport_avail - $post_avail))
else
	avail_reimport_diff=$(($post_avail - $reimport_avail))
fi
typeset avail_reimport_tol=$(($post_avail / 100))
if [[ $avail_reimport_diff -gt $avail_reimport_tol ]]; then
	log_fail "Dataset available changed significantly after reimport" \
	    "(before=$post_avail after=$reimport_avail diff=$avail_reimport_diff)"
fi

typeset reimport_sum=$(($reimport_alloc + $reimport_free))
typeset reimport_diff
if [[ $reimport_sum -gt $reimport_size ]]; then
	reimport_diff=$(($reimport_sum - $reimport_size))
else
	reimport_diff=$(($reimport_size - $reimport_sum))
fi
if [[ $reimport_diff -gt $post_tolerance ]]; then
	log_fail "After reimport: alloc+free differs from size by" \
	    "$reimport_diff (tolerance: $post_tolerance)"
fi

verify_pool $pool

# ============================================================
# USABLE accuracy: verify the dspace correction excludes allocated space.
#
# After expansion, the dspace correction adjusts USABLE to reflect
# the new geometry.  If the correction covers total vdev space
# (allocated + free) instead of just the free portion, USABLE is
# inflated and approaches total * R_new even when the pool has
# significant old-geometry data.
#
# With the correct (free-only) correction, USABLE should be
# noticeably below total * R_new because old-geometry blocks
# deflate at the lower R_old.  We verify this by computing the
# geometric maximum (total * (ndisks-nparity)/ndisks) and checking
# that USABLE is below it by at least the expected alloc * delta.
# ============================================================

typeset post2_alloc=$(zpool list -Hpo allocated $pool)
typeset post2_size=$(zpool list -Hpo size $pool)
typeset post2_usable=$(zpool list -Hpo usable $pool)

# Geometric max: the usable capacity if correction applied to total.
# RAIDZ2 with 4 disks after expansion: (4 - 2) / 4 = 1/2.
typeset usable_max=$(($post2_size / 2))

# The over-correction (alloc * (R_new - R_old)) makes usable approach
# usable_max.  With correct free-only correction and >20% pool
# utilization, usable should be well below usable_max.
typeset fill_pct=$(($post2_alloc * 100 / $post2_size))

log_note "USABLE check: usable=$post2_usable usable_max=$usable_max" \
    "alloc=$post2_alloc fill=${fill_pct}%"

if [[ $fill_pct -gt 20 ]]; then
	# With >20% fill, the gap between usable and usable_max should
	# be at least 5% of usable_max.  The buggy total correction
	# leaves a gap of <1%; the correct free-only correction leaves
	# a gap proportional to fill_pct * (R_new - R_old).
	typeset usable_gap=$(($usable_max - $post2_usable))
	typeset min_gap=$(($usable_max * 5 / 100))
	log_note "USABLE gap: gap=$usable_gap min_expected=$min_gap"
	if [[ $usable_gap -lt $min_gap ]]; then
		log_fail "USABLE ($post2_usable) is within ${usable_gap}" \
		    "bytes of geometric max ($usable_max)." \
		    "Expected gap >= $min_gap for ${fill_pct}% fill." \
		    "dspace correction is applied to total instead" \
		    "of free space."
	fi
fi

log_pass "RAIDZ expansion space accounting is correct."
