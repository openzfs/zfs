#!/usr/bin/ksh -p

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
#	A pool should be importable even if device paths have changed.
#
# STRATEGY:
#	1. Create a pool.
#	2. Export the pool.
#	3. Change the paths of some of the devices.
#	4. Verify that we can import the pool in a healthy state.
#

verify_runnable "global"

log_onexit cleanup

function test_new_paths
{
	typeset poolcreate="$1"
	typeset pathstochange="$2"

	log_note "$0: pool '$poolcreate', changing paths of $pathstochange."

	log_must zpool create $TESTPOOL1 $poolcreate

	log_must zpool export $TESTPOOL1

	for dev in $pathstochange; do
		log_must mv $dev "${dev}_new"
	done

	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	log_must check_pool_healthy $TESTPOOL1

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	for dev in $pathstochange; do
		log_must mv "${dev}_new" $dev
	done

	log_note ""
}

function test_swap_paths
{
	typeset poolcreate="$1"
	typeset pathtoswap1="$2"
	typeset pathtoswap2="$3"

	log_note "$0: pool '$poolcreate', swapping paths of $pathtoswap1" \
	    "and $pathtoswap2."

	log_must zpool create $TESTPOOL1 $poolcreate

	log_must zpool export $TESTPOOL1

	log_must mv $pathtoswap2 "$pathtoswap2.tmp"
	log_must mv $pathtoswap1 "$pathtoswap2"
	log_must mv "$pathtoswap2.tmp" $pathtoswap1

	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	log_must check_pool_healthy $TESTPOOL1

	# Cleanup
	log_must zpool destroy $TESTPOOL1

	log_note ""
}

test_new_paths "$VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_new_paths "mirror $VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_new_paths "$VDEV0 log $VDEV1" "$VDEV1"
test_new_paths "raidz $VDEV0 $VDEV1 $VDEV2" "$VDEV1"

test_swap_paths "$VDEV0 $VDEV1" "$VDEV0" "$VDEV1"
test_swap_paths "raidz $VDEV0 $VDEV1 $VDEV2" "$VDEV0" "$VDEV1"
test_swap_paths "mirror $VDEV0 $VDEV1 mirror $VDEV2 $VDEV3" \
    "$VDEV0" "$VDEV2"

log_pass "zpool import succeeded after changing device paths."
