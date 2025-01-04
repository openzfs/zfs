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
# Copyright (c) 2021 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A pool should be importable from a cachefile even if device paths
#	have changed.
#
# STRATEGY:
#	1. Create a pool using a cachefile
#	2. Backup cachefile
#	3. Export the pool.
#	4. Change the paths of some of the devices.
#	5. Verify that we can import the pool using the cachefile.
#

verify_runnable "global"

log_onexit cleanup

function test_new_paths
{
	typeset poolcreate="$1"
	typeset pathstochange="$2"

	log_note "$0: pool '$poolcreate', changing paths of $pathstochange."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate

	log_must cp $CPATH $CPATHBKP

	log_must zpool export $TESTPOOL1

	for dev in $pathstochange; do
		log_must mv $dev "${dev}_new"
	done

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_healthy $TESTPOOL1

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	for dev in $pathstochange; do
		log_must mv "${dev}_new" $dev
	done

	log_note ""
}

function test_duplicate_pools
{
	typeset poolcreate="$1"
	typeset pathstocopy="$2"

	log_note "$0: pool '$poolcreate', creating duplicate pool using $pathstocopy."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate
	log_must zpool export $TESTPOOL1

	for dev in $pathstocopy; do
		log_must cp $dev "${dev}_orig"

	done

	log_must zpool create -f -o cachefile=$CPATH $TESTPOOL1 $poolcreate
	log_must cp $CPATH $CPATHBKP
	log_must zpool export $TESTPOOL1

	for dev in $pathstocopy; do
		log_must mv $dev "${dev}_new"
	done

	log_must zpool import -c $CPATHBKP
	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_healthy $TESTPOOL1

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	for dev in $pathstocopy; do
		log_must rm "${dev}_orig"
		log_must mv "${dev}_new" $dev
	done

	log_note ""
}

test_new_paths "$VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_new_paths "mirror $VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_new_paths "$VDEV0 log $VDEV1" "$VDEV0 $VDEV1"
test_new_paths "raidz $VDEV0 $VDEV1 $VDEV2" "$VDEV0 $VDEV1 $VDEV2"
test_new_paths "draid $VDEV0 $VDEV1 $VDEV2" "$VDEV0 $VDEV1 $VDEV2"

test_duplicate_pools "$VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_duplicate_pools "mirror $VDEV0 $VDEV1" "$VDEV0 $VDEV1"
test_duplicate_pools "$VDEV0 log $VDEV1" "$VDEV0 $VDEV1"
test_duplicate_pools "raidz $VDEV0 $VDEV1 $VDEV2" "$VDEV0 $VDEV1 $VDEV2"
test_duplicate_pools "draid $VDEV0 $VDEV1 $VDEV2" "$VDEV0 $VDEV1 $VDEV2"

log_pass "zpool import with cachefile succeeded after changing device paths."
