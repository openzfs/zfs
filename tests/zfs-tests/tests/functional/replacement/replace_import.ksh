#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# Description:
# Verify that on import an in progress replace operation is resumed.
#
# Strategy:
# 1. For both healing and sequential resilvering replace:
#    a. Create a pool
#    b. Replace a vdev with 'zpool replace' to resilver (-s) it.
#    c. Export the pool
#    d. Import the pool
#    e. Verify the 'zpool replace' resumed resilvering.
#    f. Destroy the pool
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE
}

log_assert "Verify replace is resumed on import"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} $SPARE_VDEV_FILE

# Verify healing and sequential resilver resume on import.
for arg in "" "-s"; do
	log_must zpool create -f $TESTPOOL1 ${VDEV_FILES[@]}
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE
	log_must is_pool_resilvering $TESTPOOL1
	log_must zpool export $TESTPOOL1
	log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
	log_must is_pool_resilvering $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	log_must zpool wait -t resilver $TESTPOOL1
	log_must is_pool_resilvered $TESTPOOL1
	destroy_pool $TESTPOOL1
done

log_pass "Verify replace is resumed on import"
