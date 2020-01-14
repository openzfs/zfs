#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.kshlib
. $STF_SUITE/tests/functional/trim/trim.cfg

#
# DESCRIPTION:
# 	Check various pool geometries stripe, mirror, raidz)
#
# STRATEGY:
#	1. Create a pool on file vdevs to trim.
#	2. Set 'autotrim=on' on pool.
#	3. Fill the pool to a known percentage of capacity.
#	4. Verify the vdevs contain 75% or more allocated blocks.
#	5. Remove all files making it possible to trim the entire pool.
#	6. Wait for auto trim to issue trim IOs for the free blocks.
#	7. Verify the disks contain 30% or less allocated blocks.
#	8. Repeat for test for striped, mirrored, and RAIDZ pools.

verify_runnable "global"

log_assert "Set 'autotrim=on' verify pool disks were trimmed"

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	log_must rm -f $TRIM_VDEVS

	log_must set_tunable64 TRIM_EXTENT_BYTES_MIN $trim_extent_bytes_min
	log_must set_tunable64 TRIM_TXG_BATCH $trim_txg_batch
	log_must set_tunable64 VDEV_MIN_MS_COUNT $vdev_min_ms_count
}
log_onexit cleanup

# Minimum trim size is decreased to verify all trim sizes.
typeset trim_extent_bytes_min=$(get_tunable TRIM_EXTENT_BYTES_MIN)
log_must set_tunable64 TRIM_EXTENT_BYTES_MIN 4096

# Reduced TRIM_TXG_BATCH to make trimming more frequent.
typeset trim_txg_batch=$(get_tunable TRIM_TXG_BATCH)
log_must set_tunable64 TRIM_TXG_BATCH 8

# Increased metaslabs to better simulate larger more realistic devices.
typeset vdev_min_ms_count=$(get_tunable VDEV_MIN_MS_COUNT)
log_must set_tunable64 VDEV_MIN_MS_COUNT 32

typeset VDEV_MAX_MB=$(( floor(4 * MINVDEVSIZE * 0.75 / 1024 / 1024) ))
typeset VDEV_MIN_MB=$(( floor(4 * MINVDEVSIZE * 0.30 / 1024 / 1024) ))

for type in "" "mirror" "raidz2"; do

	if [[ "$type" = "" ]]; then
		VDEVS="$TRIM_VDEV1"
	elif [[ "$type" = "mirror" ]]; then
		VDEVS="$TRIM_VDEV1 $TRIM_VDEV2"
	else
		VDEVS="$TRIM_VDEV1 $TRIM_VDEV2 $TRIM_VDEV3"
	fi

	log_must truncate -s $((4 * MINVDEVSIZE)) $VDEVS
	log_must zpool create -f $TESTPOOL $VDEVS
	log_must zpool set autotrim=on $TESTPOOL

	typeset availspace=$(get_prop available $TESTPOOL)
	typeset fill_mb=$(( floor(availspace * 0.90 / 1024 / 1024) ))

	# Fill the pool, verify the vdevs are no longer sparse.
	file_write -o create -f /$TESTPOOL/file -b 1048576 -c $fill_mb -d R
	verify_vdevs "-ge" "$VDEV_MAX_MB" $VDEVS

	# Remove the file, wait for trim, verify the vdevs are now sparse.
	log_must rm /$TESTPOOL/file
	wait_trim_io $TESTPOOL "ind" 64
	verify_vdevs "-le" "$VDEV_MIN_MB" $VDEVS

	log_must zpool destroy $TESTPOOL
	log_must rm -f $VDEVS
done

log_pass "Auto trim successfully shrunk vdevs"
