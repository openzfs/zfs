#!/bin/ksh -p

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
#	that one or more vdevs were removed.
#
# STRATEGY:
#	1. Create a pool with some devices and an alternate cachefile.
#	2. Backup the cachefile.
#	3. Remove device(s) from the pool and remove them.
#	4. (Optionally) Add device(s) to pool.
#	5. Export the pool.
#	6. Verify that we can import the pool using the backed-up cachefile.
#

verify_runnable "global"

function custom_cleanup
{
	cleanup
}

log_onexit custom_cleanup

function test_remove_vdev
{
	typeset poolcreate="$1"
	typeset removevdev="$2"
	typeset poolcheck="$3"

	log_note "$0: pool '$poolcreate', remove $2."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate

	log_must cp $CPATH $CPATHBKP

	log_must zpool remove $TESTPOOL1 $removevdev
	log_must wait_for_pool_config $TESTPOOL1 "$poolcheck"
	log_must rm $removevdev

	log_must zpool export $TESTPOOL1

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolcheck"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	log_must mkfile $FILE_SIZE $removevdev

	log_note ""
}

#
# We have to remove top-level non-log vdevs one by one, else there is a high
# chance pool will report busy and command will fail for the second vdev.
#
function test_remove_two_vdevs
{
	log_note "$0."
	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 \
	    $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4

	log_must cp $CPATH $CPATHBKP

	log_must zpool remove $TESTPOOL1 $VDEV4
	log_must wait_for_pool_config $TESTPOOL1 \
	    "$VDEV0 $VDEV1 $VDEV2 $VDEV3"
	log_must zpool remove $TESTPOOL1 $VDEV3
	log_must wait_for_pool_config $TESTPOOL1 "$VDEV0 $VDEV1 $VDEV2"
	log_must rm $VDEV3 $VDEV4

	log_must zpool export $TESTPOOL1

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$VDEV0 $VDEV1 $VDEV2"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	log_must mkfile $FILE_SIZE $VDEV3 $VDEV4

	log_note ""
}

#
# We want to test the case where a whole created by a log device is filled
# by a regular device
#
function test_remove_log_then_add_vdev
{
	log_note "$0."
	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 \
	    $VDEV0 $VDEV1 $VDEV2 log $VDEV3

	log_must cp $CPATH $CPATHBKP

	log_must zpool remove $TESTPOOL1 $VDEV1
	log_must wait_for_pool_config $TESTPOOL1 "$VDEV0 $VDEV2 log $VDEV3"
	log_must zpool remove $TESTPOOL1 $VDEV3
	log_must check_pool_config $TESTPOOL1 "$VDEV0 $VDEV2"
	log_must rm $VDEV1 $VDEV3
	log_must zpool add $TESTPOOL1 $VDEV4

	log_must zpool export $TESTPOOL1

	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$VDEV0 $VDEV2 $VDEV4"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP
	log_must mkfile $FILE_SIZE $VDEV1 $VDEV3

	log_note ""
}

test_remove_vdev "$VDEV0 $VDEV1 $VDEV2" "$VDEV2" "$VDEV0 $VDEV1"
test_remove_vdev "$VDEV0 $VDEV1 $VDEV2" "$VDEV1" "$VDEV0 $VDEV2"
test_remove_vdev "$VDEV0 log $VDEV1" "$VDEV1" "$VDEV0"
test_remove_vdev "$VDEV0 log $VDEV1 $VDEV2" "$VDEV1 $VDEV2" "$VDEV0"
test_remove_vdev "$VDEV0 $VDEV1 $VDEV2 log $VDEV3" "$VDEV2" \
    "$VDEV0 $VDEV1 log $VDEV3"
test_remove_two_vdevs
test_remove_log_then_add_vdev

log_pass "zpool import -c cachefile_unaware_of_remove passed."
