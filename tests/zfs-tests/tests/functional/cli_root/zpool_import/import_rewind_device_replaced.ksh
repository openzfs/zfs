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
#	It should be possible to rewind a pool beyond a device replacement.
#
# STRATEGY:
#	1. Create a pool.
#	2. Generate files and remember their md5sum.
#	3. Sync a few times and note last synced txg.
#	4. Take a snapshot to make sure old blocks are not overwritten.
#	5. Initiate device replacement and export the pool. Special care must
#	   be taken so that resilvering doesn't complete before the export.
#	6. Test 1: Rewind pool to noted txg and then verify data checksums.
#	   Import it read-only so that we do not overwrite blocks in later txgs.
#	7. Re-import pool at latest txg and let the replacement finish.
#	8. Export the pool an remove the new device - we shouldn't need it.
#	9. Test 2: Rewind pool to noted txg and then verify data checksums.
#
# STRATEGY TO SLOW DOWN RESILVERING:
#	1. Reduce zfs_txg_timeout, which controls how long can we resilver for
#	   each sync.
#	2. Add data to pool
#	3. Re-import the pool so that data isn't cached
#	4. Use zinject to slow down device I/O
#	5. Trigger the resilvering
#	6. Use spa freeze to stop writing to the pool.
#	7. Clear zinject events (needed to export the pool)
#	8. Export the pool
#
# DISCLAIMER:
#	This test can fail since nothing guarantees that old MOS blocks aren't
#	overwritten. Snapshots protect datasets and data files but not the MOS.
#	sync_some_data_a_few_times interleaves file data and MOS data for a few
#	txgs, thus increasing the odds that some txgs will have their MOS data
#	left untouched.
#

verify_runnable "global"

ZFS_TXG_TIMEOUT=""

function custom_cleanup
{
	# Revert zfs_txg_timeout to defaults
	[[ -n $ZFS_TXG_TIMEOUT ]] &&
	    log_must set_zfs_txg_timeout $ZFS_TXG_TIMEOUT
	log_must rm -rf $BACKUP_DEVICE_DIR
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	cleanup
}

log_onexit custom_cleanup

function test_replace_vdev
{
	typeset poolcreate="$1"
	typeset replacevdev="$2"
	typeset replaceby="$3"
	typeset poolfinalstate="$4"
	typeset zinjectdevices="$5"
	typeset writedata="$6"

	log_note "$0: pool '$poolcreate', replace $replacevdev by $replaceby."

	log_must zpool create $TESTPOOL1 $poolcreate

	# generate data and checksum it
	log_must generate_data $TESTPOOL1 $MD5FILE

	# add more data so that resilver takes longer
	log_must write_some_data $TESTPOOL1 $writedata

	# Syncing a few times while writing new data increases the odds that
	# MOS metadata for some of the txgs will survive.
	log_must sync_some_data_a_few_times $TESTPOOL1
	typeset txg
	txg=$(get_last_txg_synced $TESTPOOL1)
	log_must zfs snapshot -r $TESTPOOL1@snap1

	# This should not free original data.
	log_must overwrite_data $TESTPOOL1 ""

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1

	# Ensure resilvering doesn't complete.
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace $TESTPOOL1 $replacevdev $replaceby

	# Confirm pool is still replacing
	log_must pool_is_replacing $TESTPOOL1
	log_must zpool export $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0

	############################################################
	# Test 1: rewind while device is resilvering.
	# Import read only to avoid overwriting more recent blocks.
	############################################################
	log_must zpool import -d $DEVICE_DIR -o readonly=on -T $txg $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolcreate"

	log_must verify_data_md5sums $MD5FILE

	log_must zpool export $TESTPOOL1

	# Import pool at latest txg to finish the resilvering
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	log_must overwrite_data $TESTPOOL1 ""
	log_must wait_for_pool_config $TESTPOOL1 "$poolfinalstate"
	log_must zpool export $TESTPOOL1

	# Move out the new device
	log_must mv $replaceby $BACKUP_DEVICE_DIR/

	############################################################
	# Test 2: rewind after device has been replaced.
	# Import read-write since we won't need the pool anymore.
	############################################################
	log_must zpool import -d $DEVICE_DIR -T $txg $TESTPOOL1
	log_must check_pool_config $TESTPOOL1 "$poolcreate"

	log_must verify_data_md5sums $MD5FILE

	# Cleanup
	log_must zpool destroy $TESTPOOL1
	# Restore the device we moved out
	log_must mv "$BACKUP_DEVICE_DIR/$(basename $replaceby)" $DEVICE_DIR/
	# Fast way to clear vdev labels
	log_must zpool create -f $TESTPOOL2 $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4
	log_must zpool destroy $TESTPOOL2

	log_note ""
}

# Record txg history
is_linux && log_must set_tunable32 TXG_HISTORY 100

log_must mkdir -p $BACKUP_DEVICE_DIR
# Make the devices bigger to reduce chances of overwriting MOS metadata.
increase_device_sizes $(( FILE_SIZE * 4 ))

# We set zfs_txg_timeout to 1 to reduce resilvering time at each sync.
ZFS_TXG_TIMEOUT=$(get_zfs_txg_timeout)
set_zfs_txg_timeout 1

test_replace_vdev "$VDEV0 $VDEV1" \
    "$VDEV1" "$VDEV2" \
    "$VDEV0 $VDEV2" \
    "$VDEV0 $VDEV1" 15

test_replace_vdev "mirror $VDEV0 $VDEV1" \
	"$VDEV1" "$VDEV2" \
	"mirror $VDEV0 $VDEV2" \
	"$VDEV0 $VDEV1" 10

test_replace_vdev "raidz $VDEV0 $VDEV1 $VDEV2" \
	"$VDEV1" "$VDEV3" \
	"raidz $VDEV0 $VDEV3 $VDEV2" \
	"$VDEV0 $VDEV1 $VDEV2" 10

set_zfs_txg_timeout $ZFS_TXG_TIMEOUT

log_pass "zpool import rewind after device replacement passed."
