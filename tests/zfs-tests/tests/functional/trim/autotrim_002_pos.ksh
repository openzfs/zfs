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
# Copyright (c) 2017 by Tim Chase. All rights reserved.
# Copyright (c) 2017 by Nexenta Systems, Inc. All rights reserved.
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.cfg
. $STF_SUITE/tests/functional/trim/trim.kshlib

#
# DESCRIPTION:
# 	Check various pool geometries (raidz[1-3], mirror, stripe)
#
# STRATEGY:
#	1. Create a pool on file vdevs to TRIM.
#	2. Set 'autotrim=on' on pool.
#	3. Fill the pool to a known percentage of capacity.
#	4. Verify the vdevs contain 25% or more allocated blocks.
#	5. Remove all files making the free blocks TRIMable.
#	6. Wait for autotrim to issue TRIM IOs for the free blocks.
#	4. Verify the vdevs contain 5% or less allocated blocks.
#	8. Repeat for test for striped, mirrored, and RAIDZ pools.

verify_runnable "global"

log_assert "Set 'autotrim=on' verify pool vdevs shrink"
log_onexit cleanup_trim

# Minimum TRIM size is descreased to verity all TRIM sizes.
set_tunable64 zfs_trim_min_ext_sz 4096

# Reduced zfs_txgs_per_trim to make TRIMing more frequent.
set_tunable32 zfs_txgs_per_trim 2

typeset vdev_max_mb=$(( floor(VDEV_SIZE * 0.25 / 1024 / 1024) ))
typeset vdev_min_mb=$(( floor(VDEV_SIZE * 0.05 / 1024 / 1024) ))

for type in "" "mirror" "raidz" "raidz2" "raidz3"; do
	log_must truncate -s $VDEV_SIZE $VDEVS
	log_must zpool create -o cachefile=none -f $TRIMPOOL $type $VDEVS
	log_must zpool set autotrim=on $TRIMPOOL

	# Fill pool.  Striped, mirrored, and raidz pools are filled to
	# different capacities due to differences in the reserved space.
	typeset availspace=$(get_prop available $TRIMPOOL)
	if [[ "$type" = "mirror" ]]; then
		typeset fill_mb=$(( floor(availspace * 0.65 / 1024 / 1024) ))
	elif [[ "$type" = "" ]]; then
		typeset fill_mb=$(( floor(availspace * 0.35 / 1024 / 1024) ))
	else
		typeset fill_mb=$(( floor(availspace * 0.40 / 1024 / 1024) ))
	fi

	log_must file_write -o create -f /$TRIMPOOL/$TESTFILE \
	    -b 1048576 -c $fill_mb -d R
	log_must zpool sync
	check_vdevs "-gt" "$vdev_max_mb"

	# Remove the file vdev usage should drop to less than 5%.
	log_must rm /$TRIMPOOL/$TESTFILE
	wait_trim_io $TRIMPOOL "auto" 10
	check_vdevs "-le" "$vdev_min_mb"

	log_must zpool destroy $TRIMPOOL
	log_must rm -f $VDEVS
done

log_pass "Auto TRIM successfully shrunk vdevs"
