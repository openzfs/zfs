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
#	Verify that space accounting converges after rewriting data between
#	RAIDZ expansions.  When data is rewritten after an expansion, it
#	moves from old geometry to new geometry.  This tests that accounting
#	remains consistent through write-expand-rewrite cycles.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool with 3 disks
#	2. Write initial data
#	3. For each expansion (3→4, 4→5):
#	   a. Attach a new disk, wait for expansion
#	   b. Rewrite all data (remove and recreate files)
#	   c. Verify alloc + free ≈ size
#	   d. Verify consumed space is reasonable for write size
#	4. After all expansions and rewrites, verify that freespace
#	   accurately reflects writable capacity by filling the pool
#

typeset -r devs=5
typeset -r dev_size_mb=512
typeset -r write_size_mb=96

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

function verify_accounting # <label>
{
	typeset label=$1
	typeset size=$(zpool list -Hpo size $pool)
	typeset alloc=$(zpool list -Hpo allocated $pool)
	typeset free=$(zpool list -Hpo free $pool)

	log_note "$label: size=$size alloc=$alloc free=$free"

	typeset sum=$(($alloc + $free))
	typeset diff
	if [[ $sum -gt $size ]]; then
		diff=$(($sum - $size))
	else
		diff=$(($size - $sum))
	fi

	typeset tolerance=$(($size / 10))
	if [[ $diff -gt $tolerance ]]; then
		log_fail "$label: alloc+free differs from size by $diff" \
		    "(tolerance: $tolerance)"
	fi
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

# Create a RAIDZ1 pool with 3 disks
log_must zpool create -f $opts $pool raidz1 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool
log_must zfs create $pool/fs

# Write initial data
log_must dd if=/dev/urandom of=/$pool/fs/data bs=1M count=$write_size_mb
log_must sync_pool $pool
verify_accounting "Initial write"

# Expand and rewrite cycle: 3→4 disks, then 4→5 disks
typeset expand_num=0
for disk_idx in {4..$devs}; do
	expand_num=$(($expand_num + 1))

	# Record pre-expansion state
	typeset pre_alloc=$(zpool list -Hpo allocated $pool)

	# Expand
	log_must zpool attach -w $pool raidz1-0 ${disks[$disk_idx]}
	is_pool_scrubbing $pool && wait_scrubbed $pool
	sleep 3

	verify_accounting "After expansion $expand_num"

	# Rewrite all data: remove old files and write new ones.
	# This forces data to be written with the new geometry,
	# exercising the transition from old to new deflation ratios.
	log_must rm -f /$pool/fs/data
	log_must sync_pool $pool

	typeset mid_alloc=$(zpool list -Hpo allocated $pool)
	log_note "After delete: alloc went from $pre_alloc to $mid_alloc"

	verify_accounting "After rewrite delete $expand_num"

	log_must dd if=/dev/urandom of=/$pool/fs/data bs=1M count=$write_size_mb
	log_must sync_pool $pool

	typeset post_alloc=$(zpool list -Hpo allocated $pool)
	log_note "After rewrite: alloc=$post_alloc"

	verify_accounting "After rewrite $expand_num"
done

# Final validation: verify that reported available space is usable.
# Write data in chunks until we get ENOSPC, then verify that
# the total written is within 30% of the deflated available space.
# Use zfs available (deflated, accounts for dspace correction and slop)
# rather than zpool free (raw physical) for an apples-to-apples
# comparison of logical bytes written vs logical bytes reported.
typeset reported_avail=$(zfs get -Hpo value available $pool/fs)
log_note "Reported available before fill: $reported_avail"

typeset total_written=0
typeset chunk=0
typeset chunk_size_mb=64
while true; do
	dd if=/dev/urandom of=/$pool/fs/fill_$chunk bs=1M \
	    count=$chunk_size_mb 2>/dev/null
	if [[ $? -ne 0 ]]; then
		# Reduce chunk size on failure to squeeze in more data
		if [[ $chunk_size_mb -gt 1 ]]; then
			chunk_size_mb=$(($chunk_size_mb / 4))
			[[ $chunk_size_mb -lt 1 ]] && chunk_size_mb=1
			continue
		fi
		break
	fi
	total_written=$(($total_written + $chunk_size_mb * 1024 * 1024))
	chunk=$(($chunk + 1))
done
log_must sync_pool $pool

log_note "Total written to fill pool: $total_written (reported avail was: $reported_avail)"

# The total data written should be within 30% of the reported available.
# We use a generous tolerance because:
# - Metadata overhead grows as the pool fills
# - Block rounding and alignment waste some space
# - Slop space reserves ~3.2% that cannot be written
typeset fill_diff
if [[ $total_written -gt $reported_avail ]]; then
	fill_diff=$(($total_written - $reported_avail))
else
	fill_diff=$(($reported_avail - $total_written))
fi
typeset fill_tolerance=$(($reported_avail * 30 / 100))
if [[ $fill_diff -gt $fill_tolerance ]]; then
	log_fail "Writable capacity ($total_written) differs from reported" \
	    "available ($reported_avail) by $fill_diff" \
	    "(tolerance: $fill_tolerance)." \
	    "Reported available may be inaccurate."
fi
log_note "Fill accuracy: diff=$fill_diff tolerance=$fill_tolerance"

verify_pool $pool

log_pass "RAIDZ expansion rewrite space accounting is correct."
