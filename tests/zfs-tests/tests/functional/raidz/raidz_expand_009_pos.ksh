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
#	Verify that space accounting remains accurate across multiple
#	sequential RAIDZ expansions.  After each expansion, write data and
#	confirm that the reported freespace, consumed space, and write size
#	all agree within an acceptable tolerance.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool with 3 disks
#	2. For each additional disk (4th, 5th, 6th):
#	   a. Record free space before writing
#	   b. Write a known amount of data
#	   c. Verify that consumed space increased by approximately the
#	      write size (accounting for parity and metadata overhead)
#	   d. Verify alloc + free ≈ size
#	   e. Attach the next disk and wait for expansion
#	   f. Verify accounting consistency after expansion
#	3. Verify the pool
#

typeset -r devs=6
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

#
# Verify that alloc + free ≈ size within 10% tolerance.
# A wider tolerance is needed here because multiple expansions with
# mixed-geometry blocks create more metadata overhead variance.
#
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

	log_note "$label: accounting consistent (diff=$diff tolerance=$tolerance)"
}

#
# Write a known amount of data and verify that consumed space changed
# by a reasonable amount.  For RAIDZ, the actual consumption will be
# larger than the logical write due to parity, but should not be wildly
# different.  We check that consumed space increased by at least 50%
# of the write size (parity overhead won't eat more than half) and no
# more than 3x (accounting for parity + metadata + rounding).
#
function write_and_verify_consumption # <file> <size_mb>
{
	typeset file=$1
	typeset size_mb=$2
	typeset write_bytes=$(($size_mb * 1024 * 1024))

	typeset pre_alloc=$(zpool list -Hpo allocated $pool)
	typeset pre_free=$(zpool list -Hpo free $pool)

	log_must dd if=/dev/urandom of=$file bs=1M count=$size_mb
	log_must sync_pool $pool

	typeset post_alloc=$(zpool list -Hpo allocated $pool)
	typeset post_free=$(zpool list -Hpo free $pool)

	typeset consumed=$(($post_alloc - $pre_alloc))
	typeset free_decreased=$(($pre_free - $post_free))

	log_note "Wrote ${size_mb}MB: consumed=$consumed free_decreased=$free_decreased"

	# Consumed space should be at least 50% of logical write
	typeset min_consumed=$(($write_bytes / 2))
	if [[ $consumed -lt $min_consumed ]]; then
		log_fail "Space consumed ($consumed) is less than 50% of" \
		    "write size ($write_bytes) — accounting may be wrong"
	fi

	# Consumed space should not exceed 3x logical write
	typeset max_consumed=$(($write_bytes * 3))
	if [[ $consumed -gt $max_consumed ]]; then
		log_fail "Space consumed ($consumed) is more than 3x" \
		    "write size ($write_bytes) — accounting may be wrong"
	fi

	# Free space decrease should roughly match consumed increase
	# Allow them to differ by up to 20% of consumed
	typeset cf_diff
	if [[ $consumed -gt $free_decreased ]]; then
		cf_diff=$(($consumed - $free_decreased))
	else
		cf_diff=$(($free_decreased - $consumed))
	fi
	typeset cf_tolerance=$(($consumed / 5))
	if [[ $cf_diff -gt $cf_tolerance ]]; then
		log_fail "consumed ($consumed) and free decrease" \
		    "($free_decreased) differ by $cf_diff" \
		    "(tolerance: $cf_tolerance)"
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

# Create a RAIDZ1 pool with 3 disks (2 data + 1 parity)
log_must zpool create -f $opts $pool raidz1 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool
log_must zfs create $pool/fs

verify_accounting "Initial"

# Write initial data and verify
write_and_verify_consumption /$pool/fs/file0 64
verify_accounting "After initial write"

# Perform sequential expansions: 3→4, 4→5, 5→6 disks
typeset expand_num=0
for disk_idx in {4..$devs}; do
	expand_num=$(($expand_num + 1))

	# Write data before expansion
	write_and_verify_consumption /$pool/fs/file_pre_expand_$expand_num 64
	verify_accounting "Before expansion $expand_num"

	# Record pre-expansion size
	typeset pre_size=$(zpool list -Hpo size $pool)

	# Expand
	log_must zpool attach -w $pool raidz1-0 ${disks[$disk_idx]}
	is_pool_scrubbing $pool && wait_scrubbed $pool
	sleep 3

	# Verify capacity increased
	typeset post_size=$(zpool list -Hpo size $pool)
	if [[ $post_size -le $pre_size ]]; then
		log_fail "Expansion $expand_num: pool size did not increase" \
		    "(before=$pre_size after=$post_size)"
	fi
	log_note "Expansion $expand_num: size $pre_size -> $post_size"

	verify_accounting "After expansion $expand_num"

	# Write data after expansion and verify consumption
	write_and_verify_consumption /$pool/fs/file_post_expand_$expand_num 64
	verify_accounting "After post-expansion write $expand_num"
done

verify_pool $pool

log_pass "RAIDZ multi-expansion space accounting is correct."
