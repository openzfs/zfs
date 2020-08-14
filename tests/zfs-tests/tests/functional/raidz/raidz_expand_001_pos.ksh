#!/bin/ksh -p
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
# Copyright (c) 2020 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zpool attach poolname raidz ...' should attach new devive to the pool.
#
# STRATEGY:
#	1. Create block device files for the test raidz pool
#	2. For each parity value [1..3]
#	    - create raidz pool
#	    - fill it with some directories/files
#	    - attach device to the raidz pool
#	    - verify that device attached and the raidz pool size increase
#	    - verify resilver by replacing parity devices
#	    - verify resilver by replacing data devices
#	    - verify scrub by zeroing parity devices
#	    - verify scrub by zeroing data devices
#	    - verify the raidz pool
#	    - destroy the raidz pool

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

function wait_expand_completion
{
	while zpool status $TESTPOOL | grep 'raidz expand:' | \
	    grep 'in progress'; do
		sleep 1
	done
}

function test_resilver # <pool> <parity> <dir>
{
	typeset pool=$1
	typeset nparity=$2
	typeset dir=$3

	for (( i=0; i<$nparity; i=i+1 )); do
		log_must zpool offline $pool $dir/dev-$i
	done

	log_must zpool export $pool

	for (( i=0; i<$nparity; i=i+1 )); do
		log_must zpool labelclear -f $dir/dev-$i
	done

	log_must zpool import -o cachefile=none -d $dir $pool

	for (( i=0; i<$nparity; i=i+1 )); do
		log_must zpool replace -f $pool $dir/dev-$i
	done

	while ! is_pool_resilvered $pool; do
		sleep 1
	done

	log_must check_pool_status $pool "errors" "No known data errors"

	log_must zpool clear $pool

	for (( i=$nparity; i<$nparity*2; i=i+1 )); do
		log_must zpool offline $pool $dir/dev-$i
	done

	log_must zpool export $pool

	for (( i=$nparity; i<$nparity*2; i=i+1 )); do
		log_must zpool labelclear -f $dir/dev-$i
	done

	log_must zpool import -o cachefile=none -d $dir $pool

	for (( i=$nparity; i<$nparity*2; i=i+1 )); do
		log_must zpool replace -f $pool $dir/dev-$i
	done

	while ! is_pool_resilvered $pool; do
		sleep 1
	done

	log_must check_pool_status $pool "errors" "No known data errors"

	log_must zpool clear $pool
}

function test_scrub # <pool> <parity> <dir>
{
	typeset pool=$1
	typeset nparity=$2
	typeset dir=$3
	typeset combrec=$4

	log_must zpool export $pool

	for (( i=0; i<$nparity; i=i+1 )); do
		dd conv=notrunc if=/dev/zero of=$dir/dev-$i \
		    bs=1M seek=4 count=$(($dev_size_mb-4))
	done

	log_must zpool import -o cachefile=none -d $dir $pool

	log_must zpool scrub $pool

	while ! is_pool_scrubbed $pool; do
		sleep 1
	done

	log_must zpool clear $pool

	log_must zpool export $pool

	for (( i=$nparity; i<$nparity*2; i=i+1 )); do
		dd conv=notrunc if=/dev/zero of=$dir/dev-$i \
		    bs=1M seek=4 count=$(($dev_size_mb-4))
	done

	log_must zpool import -o cachefile=none -d $dir $pool

	log_must zpool scrub $pool

	while ! is_pool_scrubbed $pool; do
		sleep 1
	done

	log_must check_pool_status $pool "errors" "No known data errors"

	log_must zpool clear $pool
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1

# Disk files which will be used by pool
for i in {0..$(($devs - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

# Disk file which will be attached
log_must truncate -s 512M $TEST_BASE_DIR/dev-$devs

for nparity in 1 2 3; do
	raid=raidz$nparity
	dir=$TEST_BASE_DIR

	log_must zpool create -f -o cachefile=none $TESTPOOL $raid ${disks[@]}
	log_must zfs set primarycache=metadata $TESTPOOL

	log_must zfs create $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 512 100 1024 R

	log_must zfs create -o compress=on $TESTPOOL/fs2
	log_must fill_fs /$TESTPOOL/fs2 1 512 100 1024 R

	log_must zfs create -o compress=on -o recordsize=8k $TESTPOOL/fs3
	log_must fill_fs /$TESTPOOL/fs3 1 512 100 1024 R

	typeset pool_size=$(get_pool_prop size $TESTPOOL)

	log_must zpool attach $TESTPOOL ${raid}-0 $dir/dev-$devs

	wait_expand_completion

	log_must zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $dir $TESTPOOL

	typeset disk_attached=$(get_disklist $TESTPOOL | grep dev-$devs)
	if [[ -z $disk_attached ]]; then
		log_fail "pool $TESTPOOL attached disk not found"
	fi

	typeset expand_size=$(get_pool_prop size $TESTPOOL)
	if [[ "$expand_size" -le "$pool_size" ]]; then
		log_fail "pool $TESTPOOL not expanded"
	fi

	log_must zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $dir $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	test_resilver $TESTPOOL $nparity $dir
	test_scrub $TESTPOOL $nparity $dir

	zpool destroy "$TESTPOOL"
done

log_pass "raidz expansion test succeeded."
