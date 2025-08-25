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
#	of a zpool replace operation at different stages in time.
#
# STRATEGY:
#	1. Create a pool with some devices and an alternate cachefile.
#	2. Backup the cachefile.
#	3. Initiate device replacement, backup cachefile again and export pool.
#	   Special care must be taken so that resilvering doesn't complete
#	   before we exported the pool.
#	4. Verify that we can import the pool using the first cachefile backup.
#	   (Test 1. cachefile: pre-replace, pool: resilvering)
#	5. Wait for the resilvering to finish and export the pool.
#	6. Verify that we can import the pool using the first cachefile backup.
#	   (Test 2. cachefile: pre-replace, pool: post-replace)
#	7. Export the pool.
#	8. Verify that we can import the pool using the second cachefile backup.
#	   (Test 3. cachefile: resilvering, pool: post-replace)
#
# STRATEGY TO SLOW DOWN RESILVERING:
#	1. Reduce zfs_txg_timeout, which controls how long can we resilver for
#	   each sync.
#	2. Add data to pool
#	3. Re-import the pool so that data isn't cached
#	4. Use zfs_scan_suspend_progress to ensure resilvers don't progress
#	5. Trigger the resilvering
#	6. Use spa freeze to stop writing to the pool.
#	7. Re-enable scan progress
#	8. Export the pool
#

verify_runnable "global"

ZFS_TXG_TIMEOUT=""

function custom_cleanup
{
	# Revert zfs_txg_timeout to defaults
	[[ -n ZFS_TXG_TIMEOUT ]] &&
	    log_must set_zfs_txg_timeout $ZFS_TXG_TIMEOUT

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	cleanup
}

log_onexit custom_cleanup

function test_replacing_vdevs
{
	typeset poolcreate="$1"
	typeset replacevdev="$2"
	typeset replaceby="$3"
	typeset poolfinalstate="$4"
	typeset zinjectdevices="$5"
	typeset earlyremove="$6"
	typeset writedata="$7"

	log_note "$0: pool '$poolcreate', replace $replacevdev by $replaceby."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $poolcreate

	# Cachefile: pool in pre-replace state
	log_must cp $CPATH $CPATHBKP

	# Steps to insure resilvering happens very slowly.
	log_must write_some_data $TESTPOOL1 $writedata
	log_must zpool export $TESTPOOL1
	log_must cp $CPATHBKP $CPATH
	log_must zpool import -c $CPATH -o cachefile=$CPATH $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace $TESTPOOL1 $replacevdev $replaceby

	# Cachefile: pool in resilvering state
	log_must cp $CPATH $CPATHBKP2

	# Confirm pool is still replacing
	log_must pool_is_replacing $TESTPOOL1
	log_must zpool export $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0

	( $earlyremove ) && log_must rm $replacevdev

	############################################################
	# Test 1. Cachefile: pre-replace, pool: resilvering
	############################################################
	log_must cp $CPATHBKP $CPATH
	log_must zpool import -c $CPATH $TESTPOOL1

	# Wait for resilvering to finish
	log_must wait_for_pool_config $TESTPOOL1 "$poolfinalstate"
	log_must zpool export $TESTPOOL1

	( ! $earlyremove ) && log_must rm $replacevdev

	############################################################
	# Test 2. Cachefile: pre-replace, pool: post-replace
	############################################################
	log_must zpool import -c $CPATHBKP $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolfinalstate"
	log_must zpool export $TESTPOOL1

	############################################################
	# Test 3. Cachefile: resilvering, pool: post-replace
	############################################################
	log_must zpool import -c $CPATHBKP2 $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolfinalstate"

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $CPATH $CPATHBKP $CPATHBKP2
	log_must mkfile $FILE_SIZE $replacevdev

	log_note ""
}

# We set zfs_txg_timeout to 1 to reduce resilvering time at each sync.
ZFS_TXG_TIMEOUT=$(get_zfs_txg_timeout)
set_zfs_txg_timeout 1

test_replacing_vdevs "$VDEV0 $VDEV1" \
    "$VDEV1" "$VDEV2" \
    "$VDEV0 $VDEV2" \
    "$VDEV0 $VDEV1" \
    false 20

test_replacing_vdevs "mirror $VDEV0 $VDEV1" \
	"$VDEV1" "$VDEV2" \
	"mirror $VDEV0 $VDEV2" \
	"$VDEV0 $VDEV1" \
	true 10

test_replacing_vdevs "raidz $VDEV0 $VDEV1 $VDEV2" \
	"$VDEV1" "$VDEV3" \
	"raidz $VDEV0 $VDEV3 $VDEV2" \
	"$VDEV0 $VDEV1 $VDEV2" \
	true 20

test_replacing_vdevs "draid:1s $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4" \
	"$VDEV1" "$VDEV5" \
	"draid $VDEV0 $VDEV5 $VDEV2 $VDEV3 $VDEV4 spares draid1-0-0" \
	"$VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4" \
	true 30

set_zfs_txg_timeout $ZFS_TXG_TIMEOUT

log_pass "zpool import -c cachefile_unaware_of_replace passed."
