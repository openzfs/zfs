#!/bin/ksh -p
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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A pool should be importable using an outdated cachefile that is unaware
#	that a mirror was detached.
#
# STRATEGY:
#	1. Create a pool with some devices mirrored and an alternate cachefile.
#	2. Backup the cachefile.
#	3. Detach a mirror from the pool.
#	4. Export the pool.
#	5. Verify that we can import the pool using the backed-up cachefile.
#

verify_runnable "global"

log_onexit cleanup

function test_detach_vdev
{
	typeset poolcreate="$1"
	typeset poolcheck="$2"

	log_note "$0: pool '$poolcreate', detach $VDEV4."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate

	log_must cp $CPATH $CPATHBKP

	log_must zpool detach $TESTPOOL1 $VDEV4
	log_must rm -f $VDEV4

	log_must zpool export $TESTPOOL1

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolcheck"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	log_must mkfile $FILE_SIZE $VDEV4

	log_note ""
}

test_detach_vdev "mirror $VDEV0 $VDEV4" "$VDEV0"
test_detach_vdev "mirror $VDEV0 $VDEV4 mirror $VDEV1 $VDEV2" \
    "$VDEV0 mirror $VDEV1 $VDEV2"
test_detach_vdev "mirror $VDEV0 $VDEV1 $VDEV4" "mirror $VDEV0 $VDEV1"
test_detach_vdev "$VDEV0 log mirror $VDEV1 $VDEV4" "$VDEV0 log $VDEV1"

log_pass "zpool import -c cachefile_unaware_of_detach passed."
