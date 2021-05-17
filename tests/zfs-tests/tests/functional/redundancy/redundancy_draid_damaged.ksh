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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	When sequentially resilvering a dRAID pool with multiple vdevs
#	that contain silent damage a sequential resilver should never
#	introduce additional unrecoverable damage.
#
# STRATEGY:
#	1. Create block device files for the test draid pool
#	2. For each parity value [1..3]
#	    - create draid pool
#	    - fill it with some directories/files
#	    - overwrite the maximum number of repairable devices
#	    - sequentially resilver each overwritten device one at a time;
#	      the device will not be correctly repaired because the silent
#	      damage on the other vdevs will cause the parity calculations
#	      to generate incorrect data for the resilvering vdev.
#	    - verify that only the resilvering devices had invalid data
#	      written and that a scrub is still able to repair the pool
#	    - destroy the draid pool
#

typeset -r devs=7
typeset -r dev_size_mb=512

typeset -a disks

prefetch_disable=$(get_tunable PREFETCH_DISABLE)
rebuild_scrub_enabled=$(get_tunable REBUILD_SCRUB_ENABLED)

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"

	for i in {0..$devs}; do
		rm -f "$TEST_BASE_DIR/dev-$i"
	done

	set_tunable32 PREFETCH_DISABLE $prefetch_disable
	set_tunable32 REBUILD_SCRUB_ENABLED $rebuild_scrub_enabled
}

function test_sequential_resilver # <pool> <parity> <dir>
{
	typeset pool=$1
	typeset nparity=$2
	typeset dir=$3

	log_must zpool export $pool

	for (( i=0; i<$nparity; i=i+1 )); do
		log_must dd conv=notrunc if=/dev/zero of=$dir/dev-$i \
		    bs=1M seek=4 count=$(($dev_size_mb-4))
	done

	log_must zpool import -o cachefile=none -d $dir $pool

	for (( i=0; i<$nparity; i=i+1 )); do
		spare=draid${nparity}-0-$i
		log_must zpool replace -fsw $pool $dir/dev-$i $spare
	done

	log_must zpool scrub -w $pool

	# When only a single child was overwritten the sequential resilver
	# can fully repair the damange from parity and the scrub will have
	# nothing to repair. When multiple children are silently damaged
	# the sequential resilver will calculate the wrong data since only
	# the parity information is used and it cannot be verified with
	# the checksum. However, since only the resilvering devices are
	# written to with the bad data a subsequent scrub will be able to
	# fully repair the pool.
	#
	if [[ $nparity == 1 ]]; then
		log_must check_pool_status $pool "scan" "repaired 0B"
	else
		log_mustnot check_pool_status $pool "scan" "repaired 0B"
	fi

	log_must check_pool_status $pool "errors" "No known data errors"
	log_must check_pool_status $pool "scan" "with 0 errors"
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

for nparity in 1 2 3; do
	raid=draid${nparity}:${nparity}s
	dir=$TEST_BASE_DIR

	log_must zpool create -f -o cachefile=none $TESTPOOL $raid ${disks[@]}
	log_must zfs set primarycache=metadata $TESTPOOL

	log_must zfs create $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 512 100 1024 R

	log_must zfs create -o compress=on $TESTPOOL/fs2
	log_must fill_fs /$TESTPOOL/fs2 1 512 100 1024 R

	log_must zfs create -o compress=on -o recordsize=8k $TESTPOOL/fs3
	log_must fill_fs /$TESTPOOL/fs3 1 512 100 1024 R

	log_must zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $dir $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	test_sequential_resilver $TESTPOOL $nparity $dir

	log_must zpool destroy "$TESTPOOL"
done

log_pass "draid damaged device(s) test succeeded."
