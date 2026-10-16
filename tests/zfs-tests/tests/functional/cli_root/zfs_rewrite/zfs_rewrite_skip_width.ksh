#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

# DESCRIPTION:
#	Verify zfs rewrite -W skips blocks whose allocated size would not
#	change, so that after a RAID-Z expansion it rewrites only the blocks
#	written before the expansion that the new width stores in less space.
#
# STRATEGY:
#	1. Create a 5-wide RAID-Z2 pool and write file A, with 128K blocks,
#	   and file S, with 4K blocks.
#	2. Expand the RAID-Z2 vdev to 6 disks and write file B.
#	3. Rewrite the three files with -W and verify that only the blocks of
#	   A moved, to the allocated size of the new width.
#	4. Rewrite them with -W again and verify nothing moved.
#	5. On the single-disk test pool, verify -W moves no block, and that
#	   a rewrite without it moves them all.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -r devs=6
typeset -r dev_size_mb=256
typeset -a disks

typeset tmp=$(mktemp)
typeset a0=$(mktemp)
typeset a1=$(mktemp)
typeset a2=$(mktemp)
typeset b0=$(mktemp)
typeset b1=$(mktemp)
typeset b2=$(mktemp)
typeset c0=$(mktemp)
typeset c1=$(mktemp)
typeset s0=$(mktemp)
typeset s1=$(mktemp)

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	for i in {0..$devs}; do
		rm -f "$TEST_BASE_DIR/dev-$i"
	done
	rm -rf $tmp $a0 $a1 $a2 $b0 $b1 $b2 $c0 $c1 $s0 $s1 $TESTDIR/* $TESTDIR1
}

# Save the DVAs (vdev:offset:asize) of the L0 blocks of a file.
function save_dvas # dataset file output
{
	log_must eval "zdb -Ovv $1 $2 > $tmp"
	log_must eval "awk '/ L0 / { print \$3 }' < $tmp > $3"
	log_must test $(wc -l < $3) -eq 8
	log_note "$2: $(echo $(<$3))"
}

# Verify that every block saved in a file has the given allocated size.
function check_asize # file asize
{
	typeset other=$(awk -F: -v asize=$2 '$3 != asize' $1)
	log_must [ -z "$other" ]
}

# Verify that no block has the same DVA in both files.
function check_moved # file1 file2
{
	typeset same=$(sort $1 $2 | uniq -d)
	log_must [ -z "$same" ]
}

log_assert "zfs rewrite -W skips blocks whose allocated size would not change"

log_onexit cleanup

for i in {0..$(($devs - 1))}; do
	log_must truncate -s ${dev_size_mb}M $TEST_BASE_DIR/dev-$i
	disks[${#disks[*]}+1]=$TEST_BASE_DIR/dev-$i
done

# With ashift=12, a 128K block takes 54 sectors on a 5-wide RAID-Z2 (32 data,
# 22 parity) and 48 on a 6-wide one (32 data, 16 parity).
log_must zpool create -f -o ashift=12 -O compression=off -O recordsize=128k \
    -m $TESTDIR1 $TESTPOOL1 raidz2 ${disks[1..$(($devs - 1))]}

log_must zfs create -o recordsize=4k $TESTPOOL1/small
log_must dd if=/dev/urandom of=$TESTDIR1/A bs=128k count=8
log_must dd if=/dev/urandom of=$TESTDIR1/small/S bs=4k count=8
log_must sync_pool $TESTPOOL1
save_dvas $TESTPOOL1 A $a0
check_asize $a0 36000
# A 4K block takes 3 sectors at either width (1 data, 2 parity).
save_dvas $TESTPOOL1/small S $s0
check_asize $s0 3000
typeset hash_a=$(xxh128digest $TESTDIR1/A)

log_must zpool attach -w $TESTPOOL1 raidz2-0 ${disks[$devs]}
log_must zpool wait -t scrub $TESTPOOL1
# Move past the txg from which allocations use the new width.
log_must sync_pool $TESTPOOL1

log_must dd if=/dev/urandom of=$TESTDIR1/B bs=128k count=8
log_must sync_pool $TESTPOOL1
save_dvas $TESTPOOL1 B $b0
check_asize $b0 30000
typeset hash_b=$(xxh128digest $TESTDIR1/B)

# The expansion moves the data but keeps the block pointers.
save_dvas $TESTPOOL1 A $a1
log_must cmp $a0 $a1

log_must zfs rewrite -W $TESTDIR1/A $TESTDIR1/B $TESTDIR1/small/S
log_must sync_pool $TESTPOOL1
save_dvas $TESTPOOL1 A $a1
check_moved $a0 $a1
check_asize $a1 30000
save_dvas $TESTPOOL1 B $b1
log_must cmp $b0 $b1
save_dvas $TESTPOOL1/small S $s1
log_must cmp $s0 $s1

log_must zfs rewrite -W $TESTDIR1/A $TESTDIR1/B $TESTDIR1/small/S
log_must sync_pool $TESTPOOL1
save_dvas $TESTPOOL1 A $a2
log_must cmp $a1 $a2
save_dvas $TESTPOOL1 B $b2
log_must cmp $b1 $b2
save_dvas $TESTPOOL1/small S $s1
log_must cmp $s0 $s1

log_must [ "$(xxh128digest $TESTDIR1/A)" = "$hash_a" ]
log_must [ "$(xxh128digest $TESTDIR1/B)" = "$hash_b" ]
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

# The test pool is not RAID-Z: its blocks are all at their current size.
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$TESTDIR/C bs=128k count=8
log_must sync_pool $TESTPOOL
save_dvas $TESTPOOL/$TESTFS C $c0

log_must zfs rewrite -W $TESTDIR/C
log_must sync_pool $TESTPOOL
save_dvas $TESTPOOL/$TESTFS C $c1
log_must cmp $c0 $c1

log_must zfs rewrite $TESTDIR/C
log_must sync_pool $TESTPOOL
save_dvas $TESTPOOL/$TESTFS C $c1
check_moved $c0 $c1

log_pass "zfs rewrite -W skips blocks whose allocated size would not change"
