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
#	On an expanded RAID-Z vdev, the pool's used space follows the space
#	charged to its datasets, and writing new blocks leaves the pool's
#	usable space unchanged, whether raidz_expansion_accounting is
#	active or not.
#
#	With the feature active, blocks born after an expansion are
#	charged at their birth ratio, which is larger than the vdev's
#	legacy ratio.  The metaslab class must count the difference as
#	allocated space; otherwise the pool's used space falls behind the
#	root dataset's by about 7% of what is written after an expansion
#	from 4 to 5 disks.  Without the feature, blocks are charged at the
#	legacy ratio, and free space must be counted at the same ratio;
#	otherwise every write on the expanded vdev lowers the usable space.
#
# STRATEGY:
#	1. Create a RAIDZ1 pool of 4 disks, write a file and expand the
#	   vdev to 5 disks
#	2. Write a second file
#	3. Require the pool's used space to match the root dataset's, and
#	   its usable space to be the same as before the write
#	4. Require the pool's used space to match the root dataset's after
#	   an export and import and after deleting the first file
#	5. Repeat on a pool created with raidz_expansion_accounting
#	   disabled
#

verify_runnable "global"

typeset -r devs=5
typeset -r dev_size_mb=1024
typeset -r pool=$TESTPOOL
typeset -r fs=$pool/fs
# Metadata outside the root dataset, such as the MOS, may differ.
typeset -r tolerance=$((1024 * 1024))

typeset -a disks

function cleanup
{
	poolexists "$pool" && log_must_busy zpool destroy "$pool"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

function check_used # <label>
{
	typeset i

	# Let deferred frees complete.
	for i in {1..3}; do
		sync_pool $pool
	done
	typeset pool_used=$(get_pool_prop used $pool)
	typeset root_used=$(get_prop used $pool)
	typeset diff=$((pool_used - root_used))

	log_note "$1: pool used $pool_used, root dataset used $root_used"
	if (( diff > tolerance || -diff > tolerance )); then
		log_fail "$1: pool used differs from root dataset by $diff"
	fi
}

function check_usable # <label> <usable before>
{
	typeset usable=$(get_pool_prop usable $pool)
	typeset diff=$((usable - $2))

	log_note "$1: usable $2 before, $usable after"
	if (( diff > tolerance || -diff > tolerance )); then
		log_fail "$1: usable changed by $diff"
	fi
}

#
# "zpool attach -w" returns when the reflow completes, before the
# configuration update that adds the vdev's new metaslabs and may count its
# free space at the new width.
#
function wait_expanded # <size before the attach>
{
	typeset -i timeout=60
	typeset -i t0=$SECONDS

	while (( $(get_pool_prop size $pool) <= $1 )); do
		if (( SECONDS - t0 > timeout )); then
			log_fail "Pool size still $1 ${timeout}s after the" \
			    "expansion"
		fi
		sleep 1
	done
}

function test_expansion # enabled|disabled
{
	typeset accounting=$1
	typeset label="Accounting $accounting"
	typeset i

	for i in {1..$devs}; do
		disks[$i]=$TEST_BASE_DIR/dev-$i
		log_must truncate -s ${dev_size_mb}M ${disks[$i]}
	done

	log_must zpool create -f -o cachefile=none \
	    -o feature@raidz_expansion_accounting=$accounting \
	    $pool raidz1 ${disks[1..4]}
	log_must zfs create -o compression=off -o recordsize=128k $fs
	log_must dd if=/dev/urandom of=/$fs/file1 bs=1M count=256
	typeset size=$(get_pool_prop size $pool)
	log_must zpool attach -w $pool raidz1-0 ${disks[5]}
	wait_expanded $size

	if [[ $accounting == "enabled" ]]; then
		typeset feat_status=$(get_pool_prop \
		    feature@raidz_expansion_accounting $pool)
		if [[ "$feat_status" != "active" ]]; then
			log_fail "Feature should be active (got: $feat_status)"
		fi
	fi

	typeset usable=$(get_pool_prop usable $pool)
	log_must dd if=/dev/urandom of=/$fs/file2 bs=1M count=256
	check_used "$label, after writing on the expanded vdev"
	check_usable "$label, after writing on the expanded vdev" $usable

	log_must zpool export $pool
	log_must zpool import -d $TEST_BASE_DIR $pool
	check_used "$label, after export and import"

	log_must rm /$fs/file1
	check_used \
	    "$label, after deleting the file written before the expansion"

	cleanup
}

log_onexit cleanup

test_expansion enabled
test_expansion disabled

log_pass "On an expanded vdev, the pool's used space follows its datasets" \
    "and writes leave its usable space unchanged."
