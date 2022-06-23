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
# Copyright (c) 2022 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	When sequentially resilvering a dRAID pool to a distributed spare
#	silent damage to an online vdev in a replacing or spare mirror vdev
#	is not expected to be repaired.  Not only does the rebuild have no
#	reason to suspect the silent damage but even if it did there's no
#	checksum available to determine the correct copy and make the repair.
#	However, the subsequent scrub should detect and repair any damage.
#
# STRATEGY:
#	1. Create block device files for the test draid pool
#	2. For each parity value [1..3]
#		a. Create a draid pool
#		b. Fill it with some directories/files
#		c. Systematically damage and replace three devices by:
#			- Overwrite the device
#			- Replace the damaged vdev with a distributed spare
#			- Scrub the pool and verify repair IO is issued
#		d. Detach the distributed spares
#		e. Scrub the pool and verify there was nothing to repair
#		f. Destroy the draid pool
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

dir=$TEST_BASE_DIR

for nparity in 1 2 3; do
	raid=draid${nparity}:3s

	log_must zpool create -f -O compression=off -o cachefile=none \
	    $TESTPOOL $raid ${disks[@]}
	# log_must zfs set primarycache=metadata $TESTPOOL

	log_must zfs create $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 256 10 1024 R

	log_must zfs create -o compress=on $TESTPOOL/fs2
	log_must fill_fs /$TESTPOOL/fs2 1 256 10 1024 R

	log_must zfs create -o compress=on -o recordsize=8k $TESTPOOL/fs3
	log_must fill_fs /$TESTPOOL/fs3 1 256 10 1024 R

	log_must zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $dir $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	for nspare in 0 1 2; do
		damaged=$dir/dev-${nspare}
		spare=draid${nparity}-0-${nspare}

		log_must zpool export $TESTPOOL
		log_must dd conv=notrunc if=/dev/zero of=$damaged \
		    bs=1M seek=4 count=$(($dev_size_mb-4))
		log_must zpool import -o cachefile=none -d $dir $TESTPOOL

		log_must zpool replace -fsw $TESTPOOL $damaged $spare

		# Scrub the pool after the sequential resilver and verify
		# that the silent damage was repaired by the scrub.
		log_must zpool scrub -w $TESTPOOL
		log_must zpool status $TESTPOOL
		log_must check_pool_status $TESTPOOL "errors" \
		    "No known data errors"
		log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
		log_mustnot check_pool_status $TESTPOOL "scan" "repaired 0B"
	done

	for nspare in 0 1 2; do
		log_must check_vdev_state $TESTPOOL \
		    spare-${nspare} "ONLINE"
		log_must check_vdev_state $TESTPOOL \
		    ${dir}/dev-${nspare} "ONLINE"
		log_must check_vdev_state $TESTPOOL \
		    draid${nparity}-0-${nspare} "ONLINE"
	done

	# Detach the distributed spares and scrub the pool again to
	# verify no damage remained on the originally corrupted vdevs.
	for nspare in 0 1 2; do
		log_must zpool detach $TESTPOOL draid${nparity}-0-${nspare}
	done

	log_must zpool clear $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	log_must zpool status $TESTPOOL

	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"

	log_must zpool destroy "$TESTPOOL"
done

log_pass "draid damaged device scrub test succeeded."
