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
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# Executing 'zpool replace -r' for raidz vdevs failed.  Rebuilds are
# only allowed for stripe/mirror pools.
#
# STRATEGY:
# 1. Create a raidz pool, verify 'zpool replace -r' fails
# 2. Create a stripe/mirror pool, verify 'zpool replace -r' passes
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE
}

log_assert "Rebuild is not allowed for raidz vdevs"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} $SPARE_VDEV_FILE

# raidz[1-3]
for vdev_type in "raidz" "raidz2" "raidz3"; do
	log_must zpool create -f $TESTPOOL1 $vdev_type ${VDEV_FILES[@]}
	log_mustnot zpool replace -r $TESTPOOL1 ${VDEV_FILES[1]} \
	    $SPARE_VDEV_FILE
	destroy_pool $TESTPOOL1
done

# stripe
log_must zpool create $TESTPOOL1 ${VDEV_FILES[@]}
log_must zpool replace -r $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
destroy_pool $TESTPOOL1

# mirror
log_must zpool create $TESTPOOL1 mirror ${VDEV_FILES[0]} ${VDEV_FILES[1]}
log_must zpool replace -r $TESTPOOL1 ${VDEV_FILES[1]}  $SPARE_VDEV_FILE
destroy_pool $TESTPOOL1

log_pass "Rebuild is not allowed for raidz vdevs"
