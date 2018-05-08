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
#	A pool should be importable using an outdated cachefile that is unaware
#	that one or two top-level vdevs were added.
#
# STRATEGY:
#	1. Create a pool with some devices and an alternate cachefile.
#	2. Backup the cachefile.
#	3. Add a device/mirror/raid to the pool.
#	4. Export the pool.
#	5. Verify that we can import the pool using the backed-up cachefile.
#

verify_runnable "global"

log_onexit cleanup

function test_add_vdevs
{
	typeset poolcreate="$1"
	typeset addvdevs="$2"
	typeset poolcheck="$3"

	log_note "$0: pool '$poolcreate', add $addvdevs."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate

	log_must cp $CPATH $CPATHBKP

	log_must zpool add -f $TESTPOOL1 $addvdevs

	log_must zpool export $TESTPOOL1

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolcheck"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP

	log_note ""
}

test_add_vdevs "$VDEV0" "$VDEV1" "$VDEV0 $VDEV1"
test_add_vdevs "$VDEV0 $VDEV1" "$VDEV2" "$VDEV0 $VDEV1 $VDEV2"
test_add_vdevs "$VDEV0" "$VDEV1 $VDEV2" "$VDEV0 $VDEV1 $VDEV2"
test_add_vdevs "$VDEV0" "mirror $VDEV1 $VDEV2" \
    "$VDEV0 mirror $VDEV1 $VDEV2"
test_add_vdevs "mirror $VDEV0 $VDEV1" "mirror $VDEV2 $VDEV3" \
    "mirror $VDEV0 $VDEV1 mirror $VDEV2 $VDEV3"
test_add_vdevs "$VDEV0" "raidz $VDEV1 $VDEV2 $VDEV3" \
    "$VDEV0 raidz $VDEV1 $VDEV2 $VDEV3"
test_add_vdevs "$VDEV0" "log $VDEV1" "$VDEV0 log $VDEV1"
test_add_vdevs "$VDEV0 log $VDEV1" "$VDEV2" "$VDEV0 $VDEV2 log $VDEV1"
test_add_vdevs "$VDEV0" "$VDEV1 log $VDEV2" "$VDEV0 $VDEV1 log $VDEV2"

log_pass "zpool import -c cachefile_unaware_of_add passed."
