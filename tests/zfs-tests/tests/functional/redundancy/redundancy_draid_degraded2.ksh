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
# Copyright (c) 2026 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2026 by Wasabi Technologies, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	When sequentially resilvering a dRAID pool with multiple vdevs
#	and N faulted vdevs, where N=parity, ensure that when another leaf
#	is marked degraded the pool can still be sequentially resilvered
#	without introducing new checksum errors.  Note we've exhausted
#	the available redundancy so no silent correction can be tolerated.
#
#	This test is very similar to redundancy_draid_degraded1 and is
#	based on it. The difference is that 1) we always have some faulted
#	vdev which is already resilvered, and 2) we resilver the most
#	recently faulted, but marked degraded due to redundancy exhaustion,
#	vdev also.
#
# STRATEGY:
#	1. Create block device files for the test draid pool
#	2. For each parity value [1..3]
#	    - create draid pool
#	    - fill it with some directories/files
#	    - fault one vdev and resilver it
#	    - fault N=parity vdevs eliminating any redundancy
#	    - force fault an additional vdev causing it to be degraded
#	    - replace faulted vdevs using a sequential resilver.
#	      The minimum pool redundancy requirements are met so
#	      reconstruction is possible when reading from all online vdevs.
#	    - verify that the draid spare was correctly reconstructed and
#	      no checksum errors were introduced.
#	    - destroy the draid pool
#

typeset -r devs=13
typeset -r dev_size_mb=512

typeset -a disks

prefetch_disable=$(get_tunable PREFETCH_DISABLE)
rebuild_scrub_enabled=$(get_tunable REBUILD_SCRUB_ENABLED)
scan_suspend_progress=$(get_tunable SCAN_SUSPEND_PROGRESS)

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"

	for i in {0..$devs}; do
		rm -f "$TEST_BASE_DIR/dev-$i"
	done

	set_tunable32 PREFETCH_DISABLE $prefetch_disable
	set_tunable32 REBUILD_SCRUB_ENABLED $rebuild_scrub_enabled
	set_tunable32 SCAN_SUSPEND_PROGRESS $scan_suspend_progress
}

function test_sequential_resilver # <pool> <parity> <dir>
{
	typeset pool=$1
	typeset nparity=$2
	typeset dir=$3

	# Fault N=parity devices
	for (( i=0; i<$nparity; i++ )); do
		log_must zpool offline -f $pool $dir/dev-$i
	done

	# Parity is exhausted, faulting another device marks it degraded
	log_must zpool offline -f $pool $dir/dev-$nparity

	# Replace all faulted vdevs with distributed spares
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	for (( i=0; i<$((nparity+1)); i++ )); do
		spare=draid${nparity}-0-$i
		log_must zpool replace -fs $pool $dir/dev-$i $spare
	done
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0

	log_must zpool wait -t resilver $pool

	log_must verify_draid_pool $pool "sequential"
}

log_onexit cleanup

log_must set_tunable32 PREFETCH_DISABLE 1
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

# Disk files which will be used by pool
for i in {0..$(($devs - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

# Disk file which will be attached
log_must truncate -s 512M $TEST_BASE_DIR/dev-$devs

for nparity in 3; do
	raid=draid${nparity}:$((nparity+2))s
	dir=$TEST_BASE_DIR

	log_must zpool create -O compression=off -f -o cachefile=none $TESTPOOL $raid ${disks[@]}
	log_must zfs set primarycache=metadata $TESTPOOL

	log_must zfs create $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 512 102400 1 R

	log_must zfs create -o compress=on $TESTPOOL/fs2
	log_must fill_fs /$TESTPOOL/fs2 1 512 102400 1 R

	log_must zfs create -o compress=on -o recordsize=8k $TESTPOOL/fs3
	log_must fill_fs /$TESTPOOL/fs3 1 512 102400 1 R

	log_must zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $dir $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	test_sequential_resilver $TESTPOOL $nparity $dir

	log_must zpool destroy "$TESTPOOL"
done

log_pass "draid degraded device(s) test succeeded."
