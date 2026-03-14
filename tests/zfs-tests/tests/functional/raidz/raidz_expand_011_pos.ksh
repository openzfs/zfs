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
#	Verify that snapshot space accounting remains consistent after
#	RAIDZ expansion.  Pre-expansion blocks held by snapshots use the
#	old (wider) parity ratio, so they legitimately consume more raw
#	space per logical byte.  This test verifies that:
#	- snapshot referenced/used sizes are stable across expansion
#	- deleting files from the active dataset correctly attributes
#	  space to the snapshot
#	- pool-level accounting (alloc+free≈size) holds throughout
#
# STRATEGY:
#	1. Create a RAIDZ1 pool with 3 disks
#	2. Write data, record referenced size, take a snapshot
#	3. Expand the pool (3→4 disks)
#	4. Verify snapshot referenced size is unchanged
#	5. Delete the file from the active dataset
#	6. Verify the snapshot now reports used space ≈ old referenced
#	7. Write new data (using new geometry)
#	8. Take a second snapshot, expand again (4→5 disks)
#	9. Delete file from active dataset again
#	10. Verify both snapshots report sensible used/referenced
#	11. Verify pool-level accounting at each step
#

typeset -r devs=5
typeset -r dev_size_mb=512
typeset -r write_size_mb=64

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

# Create a RAIDZ1 pool with 3 disks (2 data + 1 parity)
log_must zpool create -f $opts $pool raidz1 ${disks[1..3]}
log_must zfs set primarycache=metadata $pool
log_must zfs create $pool/fs

# Write pre-expansion data
log_must dd if=/dev/urandom of=/$pool/fs/data1 bs=1M count=$write_size_mb
log_must sync_pool $pool

# Record referenced size before snapshot
typeset pre_snap_ref=$(zfs get -Hpo value referenced $pool/fs)
log_note "Pre-snapshot referenced: $pre_snap_ref"

# Take snapshot of pre-expansion data
log_must zfs snapshot $pool/fs@snap1
verify_accounting "After snap1"

# Record snapshot referenced (should match dataset referenced)
typeset snap1_ref=$(zfs get -Hpo value referenced $pool/fs@snap1)
log_note "snap1 referenced: $snap1_ref"

# Expand: 3→4 disks
log_must zpool attach -w $pool raidz1-0 ${disks[4]}
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

verify_accounting "After expansion 1"

# Snapshot referenced should be unchanged after expansion —
# the blocks haven't moved, same birth txg, same ratio
typeset snap1_ref_after=$(zfs get -Hpo value referenced $pool/fs@snap1)
log_note "snap1 referenced after expansion: $snap1_ref_after"

if [[ $snap1_ref_after -ne $snap1_ref ]]; then
	log_fail "snap1 referenced changed after expansion" \
	    "(before=$snap1_ref after=$snap1_ref_after)"
fi

# Delete file from active dataset — space should move to snapshot
log_must rm -f /$pool/fs/data1
log_must sync_pool $pool

typeset snap1_used=$(zfs get -Hpo value used $pool/fs@snap1)
log_note "snap1 used after delete: $snap1_used"

# The snapshot's used should be close to its referenced
# (since all its blocks are now unique to the snapshot).
# Allow 20% tolerance for metadata and indirect blocks.
typeset snap_diff
if [[ $snap1_used -gt $snap1_ref ]]; then
	snap_diff=$(($snap1_used - $snap1_ref))
else
	snap_diff=$(($snap1_ref - $snap1_used))
fi
typeset snap_tolerance=$(($snap1_ref / 5))
if [[ $snap_diff -gt $snap_tolerance ]]; then
	log_fail "snap1 used ($snap1_used) differs from referenced" \
	    "($snap1_ref) by $snap_diff after file deletion" \
	    "(tolerance: $snap_tolerance)"
fi

verify_accounting "After delete from snap1"

# Write new data with post-expansion geometry
log_must dd if=/dev/urandom of=/$pool/fs/data2 bs=1M count=$write_size_mb
log_must sync_pool $pool

# Take second snapshot (contains post-expansion data)
log_must zfs snapshot $pool/fs@snap2
typeset snap2_ref=$(zfs get -Hpo value referenced $pool/fs@snap2)
log_note "snap2 referenced: $snap2_ref"

verify_accounting "After snap2"

# Expand again: 4→5 disks
log_must zpool attach -w $pool raidz1-0 ${disks[5]}
is_pool_scrubbing $pool && wait_scrubbed $pool
sleep 3

verify_accounting "After expansion 2"

# Both snapshot referenced values should be unchanged
typeset snap1_ref_after2=$(zfs get -Hpo value referenced $pool/fs@snap1)
typeset snap2_ref_after2=$(zfs get -Hpo value referenced $pool/fs@snap2)
log_note "After expansion 2: snap1_ref=$snap1_ref_after2 snap2_ref=$snap2_ref_after2"

if [[ $snap1_ref_after2 -ne $snap1_ref ]]; then
	log_fail "snap1 referenced changed after second expansion" \
	    "(before=$snap1_ref after=$snap1_ref_after2)"
fi
if [[ $snap2_ref_after2 -ne $snap2_ref ]]; then
	log_fail "snap2 referenced changed after second expansion" \
	    "(before=$snap2_ref after=$snap2_ref_after2)"
fi

# Delete file from active dataset — space goes to snap2
log_must rm -f /$pool/fs/data2
log_must sync_pool $pool

typeset snap2_used=$(zfs get -Hpo value used $pool/fs@snap2)
log_note "snap2 used after delete: $snap2_used"

# snap2 used should be close to snap2 referenced
typeset snap2_diff
if [[ $snap2_used -gt $snap2_ref ]]; then
	snap2_diff=$(($snap2_used - $snap2_ref))
else
	snap2_diff=$(($snap2_ref - $snap2_used))
fi
typeset snap2_tolerance=$(($snap2_ref / 5))
if [[ $snap2_diff -gt $snap2_tolerance ]]; then
	log_fail "snap2 used ($snap2_used) differs from referenced" \
	    "($snap2_ref) by $snap2_diff after file deletion" \
	    "(tolerance: $snap2_tolerance)"
fi

# snap1 should still have the same used (its blocks didn't change)
typeset snap1_used_after2=$(zfs get -Hpo value used $pool/fs@snap1)
if [[ $snap1_used_after2 -ne $snap1_used ]]; then
	log_fail "snap1 used changed after second delete" \
	    "(before=$snap1_used after=$snap1_used_after2)"
fi

verify_accounting "After all snapshot ops"

# Destroy snapshots and verify space is reclaimed properly
log_must zfs destroy $pool/fs@snap1
log_must zfs destroy $pool/fs@snap2
log_must sync_pool $pool

verify_accounting "After snapshot destroy"

verify_pool $pool

log_pass "RAIDZ expansion snapshot space accounting is correct."
