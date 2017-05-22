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
#	Verify manual trim pool data integrity.
#
# STRATEGY:
#	1. Create a pool on the provided DISKS to TRIM.
#	2. Concurrently write randomly sized files to the pool, files are
#	   written with <=128K writes with an fsync after each write.
#	3. Remove files after being written, the random nature of the IO
#	   in intended to create a wide variety of TRIMable regions.
#	4. Create and destroy snapshots and clones  to create TRIMable blocks.
#	5. Manually TRIM the pool.
#	6. Verify TRIM IOs of the expected type were issued for the pool.
#	7. Verify data integrity of the pool after TRIM.
#	8. Repeat for test for striped, mirrored, and RAIDZ pools.

verify_runnable "global"

if [ $(echo ${TRIM_DISKS} | nawk '{print NF}') -lt 2 ]; then
        log_unsupported "Too few disks available (2 disk minimum)"
fi

log_assert "Run 'zpool trim' verify pool data integrity"
log_onexit cleanup_trim

# Minimum TRIM size is descreased to verity all TRIM sizes.
set_tunable64 zfs_trim_min_ext_sz 4096

# Reduced zfs_txgs_per_trim to make TRIMing more frequent.
set_tunable32 zfs_txgs_per_trim 2

for type in "" "mirror" "raidz"; do
	log_must zpool create -o cachefile=none -f $TRIMPOOL $type $TRIM_DISKS
	write_remove
	snap_clone
	do_trim $TRIMPOOL
	check_trim_io $TRIMPOOL "man"
	check_pool $TRIMPOOL
	log_must zpool destroy $TRIMPOOL
done

log_pass "Manual TRIM successfully scrubbed vdevs"
