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
#	A pool should be importable when up to 2 top-level devices are missing.
#
# STRATEGY:
#	1. Create a pool.
#	2. Write some data to the pool and checksum it.
#	3. Add one or more devices.
#	4. Write more data to the pool and checksum it.
#	5. Export the pool.
#	6. Move added devices out of the devices directory.
#	7. Import the pool with missing devices.
#	8. Verify that the first batch of data is intact.
#	9. Verify that accessing the second batch of data doesn't suspend pool.
#	10. Export the pool, move back missing devices, Re-import the pool.
#	11. Verify that all the data is intact.
#

verify_runnable "global"

function custom_cleanup
{
	log_must set_spa_load_verify_metadata 1
	log_must set_spa_load_verify_data 1
	log_must set_zfs_max_missing_tvds 0
	log_must rm -rf $BACKUP_DEVICE_DIR
	# Highly damaged pools may fail to be destroyed, so we export them.
	poolexists $TESTPOOL1 && log_must zpool export $TESTPOOL1
	cleanup
}

log_onexit custom_cleanup

function test_devices_missing
{
	typeset poolcreate="$1"
	typeset addvdevs="$2"
	typeset missingvdevs="$3"
	typeset -i missingtvds="$4"

	log_note "$0: pool '$poolcreate', adding $addvdevs, then" \
	    "moving away $missingvdevs."

	log_must zpool create $TESTPOOL1 $poolcreate

	log_must generate_data $TESTPOOL1 $MD5FILE "first"

	log_must zpool add $TESTPOOL1 $addvdevs

	log_must generate_data $TESTPOOL1 $MD5FILE2 "second"

	log_must zpool export $TESTPOOL1

	log_must mv $missingvdevs $BACKUP_DEVICE_DIR

	# Tell zfs that it is ok to import a pool with missing top-level vdevs
	log_must set_zfs_max_missing_tvds $missingtvds
	# Missing devices means that data or metadata may be corrupted.
	(( missingtvds > 1 )) && log_must set_spa_load_verify_metadata 0
	log_must set_spa_load_verify_data 0
	log_must zpool import -o readonly=on -d $DEVICE_DIR $TESTPOOL1

	log_must verify_data_md5sums $MD5FILE

	log_note "Try reading second batch of data, make sure pool doesn't" \
	    "get suspended."
	verify_data_md5sums $MD5FILE >/dev/null 2>&1

	log_must zpool export $TESTPOOL1

	typeset newpaths=$(echo "$missingvdevs" | \
		sed "s:$DEVICE_DIR:$BACKUP_DEVICE_DIR:g")
	log_must mv $newpaths $DEVICE_DIR
	log_must set_spa_load_verify_metadata 1
	log_must set_spa_load_verify_data 1
	log_must set_zfs_max_missing_tvds 0
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1

	log_must verify_data_md5sums $MD5FILE
	log_must verify_data_md5sums $MD5FILE2

	# Cleanup
	log_must zpool destroy $TESTPOOL1

	log_note ""
}

log_must mkdir -p $BACKUP_DEVICE_DIR

test_devices_missing "$VDEV0" "$VDEV1" "$VDEV1" 1
test_devices_missing "$VDEV0" "$VDEV1 $VDEV2" "$VDEV1" 1
test_devices_missing "mirror $VDEV0 $VDEV1" "mirror $VDEV2 $VDEV3" \
    "$VDEV2 $VDEV3" 1
test_devices_missing "$VDEV0 log $VDEV1" "$VDEV2" "$VDEV2" 1

#
# Note that we are testing for 2 non-consecutive missing devices.
# Missing consecutive devices results in missing metadata. Because of
# Missing metadata can cause the root dataset to fail to mount.
#
test_devices_missing "$VDEV0" "$VDEV1 $VDEV2 $VDEV3" "$VDEV1 $VDEV3" 2

log_pass "zpool import succeeded with missing devices."
