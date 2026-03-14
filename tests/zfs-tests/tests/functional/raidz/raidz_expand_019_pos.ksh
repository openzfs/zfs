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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	With raidz_expansion_accounting active, a dataset on an expanded
#	RAID-Z vdev can be filled up to the available space it reports.
#
#	Blocks born after the accounting boundary are charged at their
#	birth ratio, which is larger than the legacy ratio after an
#	expansion.  The pool's space must be credited for the difference,
#	also across an export and import and after the blocks are freed.
#	Otherwise, with 512-byte sectors and an expansion from 3 to 4
#	disks, each byte written reduces the available space by about
#	1.11 bytes, and writes fail with ENOSPC about 10% early.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool of 3 disks and expand it to 4 disks
#	2. Check that the feature is active
#	3. Write half of the available space, then export and import
#	4. Write until ENOSPC, and require the total written to be at
#	   least 98% of the available space reported before step 3
#	5. Delete the second file, write until ENOSPC again, and require
#	   at least 98% of the available space reported after the delete
#

verify_runnable "global"

typeset -r devs=4
typeset -r dev_size_mb=512
typeset -r pool=$TESTPOOL
typeset -r fs=$pool/fs

typeset -a disks

function cleanup
{
	poolexists "$pool" && log_must_busy zpool destroy "$pool"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

#
# Write to a file until ENOSPC.
#
function fill # <file>
{
	log_mustnot dd if=/dev/urandom of=$1 bs=1M
	sync_pool $pool
}

function check_fill # <label> <written> <available>
{
	log_note "$1: wrote $2 bytes with $3 available"
	if (( $2 < $3 / 100 * 98 )); then
		log_fail "$1: wrote $2 bytes, expected at least 98% of $3"
	fi
}

log_onexit cleanup

for i in {1..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${disks[$i]}
done

log_must zpool create -f -o cachefile=none $pool raidz1 ${disks[1..3]}
log_must zfs create -o compression=off -o recordsize=128k $fs
log_must zpool attach -w $pool raidz1-0 ${disks[4]}
sync_pool $pool

feat_status=$(get_pool_prop feature@raidz_expansion_accounting $pool)
if [[ "$feat_status" != "active" ]]; then
	log_fail "Feature should be active (got: $feat_status)"
fi

avail=$(get_prop available $fs)
log_must dd if=/dev/urandom of=/$fs/file1 bs=1M count=$((avail / 2 >> 20))
sync_pool $pool

log_must zpool export $pool
log_must zpool import -d $TEST_BASE_DIR $pool

fill /$fs/file2
check_fill "First fill" \
    $(($(stat_size /$fs/file1) + $(stat_size /$fs/file2))) $avail

log_must rm /$fs/file2
sync_pool $pool
avail=$(get_prop available $fs)
fill /$fs/file3
check_fill "Second fill" $(stat_size /$fs/file3) $avail

log_pass "An expanded RAID-Z vdev can be filled up to its available space."
