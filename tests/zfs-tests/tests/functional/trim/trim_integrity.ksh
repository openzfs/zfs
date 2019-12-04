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
#	Verify manual trim pool data integrity.
#
# STRATEGY:
#	1. Create a pool on sparse file vdevs to trim.
#	2. Generate some interesting pool data which can be trimmed.
#	3. Manually trim the pool.
#	4. Verify trim IOs of the expected type were issued for the pool.
#	5. Verify data integrity of the pool after trim.
#	6. Repeat test for striped, mirrored, and RAIDZ pools.

verify_runnable "global"

log_assert "Run 'zpool trim' and verify pool data integrity"

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	log_must rm -f $TRIM_VDEVS

	log_must set_tunable64 zfs_trim_extent_bytes_min $trim_extent_bytes_min
	log_must set_tunable64 zfs_trim_txg_batch $trim_txg_batch
}
log_onexit cleanup

# Minimum trim size is decreased to verify all trim sizes.
typeset trim_extent_bytes_min=$(get_tunable zfs_trim_extent_bytes_min)
log_must set_tunable64 zfs_trim_extent_bytes_min 4096

# Reduced zfs_trim_txg_batch to make trimming more frequent.
typeset trim_txg_batch=$(get_tunable zfs_trim_txg_batch)
log_must set_tunable64 zfs_trim_txg_batch 8

for type in "" "mirror" "raidz" "raidz2" "raidz3"; do
	log_must truncate -s 1G $TRIM_VDEVS

	log_must zpool create -f $TESTPOOL $type $TRIM_VDEVS

	# Add and remove data from the pool in a random fashion in order
	# to generate a variety of interesting ranges to be manually trimmed.
	for n in {0..10}; do
		dir="/$TESTPOOL/trim-$((RANDOM % 5))"
		filesize=$((4096 + ((RANDOM * 691) % 131072) ))
		log_must rm -rf $dir
		log_must fill_fs $dir 10 10 $filesize 1 R
		zpool sync
	done
	log_must du -hs /$TESTPOOL

	log_must zpool trim $TESTPOOL
	wait_trim $TESTPOOL $TRIM_VDEVS

	verify_trim_io $TESTPOOL "ind" 10
	verify_pool $TESTPOOL

	log_must zpool destroy $TESTPOOL
	log_must rm -f $TRIM_VDEVS
done

log_pass "Manual trim successfully validated"
