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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	It should be possible to rewind a pool beyond a configuration change.
#
# STRATEGY:
#	1. Create a pool.
#	2. Generate files and remember their hashsum.
#	3. Note last synced txg.
#	4. Take a snapshot to make sure old blocks are not overwritten.
#	5. Perform zpool add/attach/detach/remove operation.
#	6. Change device paths if requested and re-import pool.
#	7. Checkpoint the pool as one last attempt to preserve old blocks.
#	8. Overwrite the files.
#	9. Export the pool.
#	10. Verify that we can rewind the pool to the noted txg.
#	11. Verify that the files are readable and retain their old data.
#
# DISCLAIMER:
#	This test can fail since nothing guarantees that old MOS blocks aren't
#	overwritten. Snapshots protect datasets and data files but not the MOS.
#	sync_some_data_a_few_times interleaves file data and MOS data for a few
#	txgs, thus increasing the odds that some txgs will have their MOS data
#	left untouched.
#

verify_runnable "global"

function custom_cleanup
{
	set_vdev_validate_skip 0
	cleanup
	log_must set_tunable64 VDEV_MIN_MS_COUNT 16
}

log_onexit custom_cleanup

function test_common
{
	typeset poolcreate="$1"
	typeset addvdevs="$2"
	typeset attachargs="${3:-}"
	typeset detachvdev="${4:-}"
	typeset removevdev="${5:-}"
	typeset finalpool="${6:-}"
	typeset retval=1

	typeset poolcheck="$poolcreate"

	log_must zpool create $TESTPOOL1 $poolcreate

	log_must generate_data $TESTPOOL1 $MD5FILE

	# syncing a few times while writing new data increases the odds that MOS
	# metadata for some of the txgs will survive
	log_must sync_some_data_a_few_times $TESTPOOL1
	typeset txg
	txg=$(get_last_txg_synced $TESTPOOL1)
	log_must zfs snapshot -r $TESTPOOL1@snap1

	#
	# Perform config change operations
	#
	if [[ -n $addvdevs ]]; then
		log_must zpool add -f $TESTPOOL1 $addvdevs
	fi
	if [[ -n $attachargs ]]; then
		log_must zpool attach $TESTPOOL1 $attachargs
	fi
	if [[ -n $detachvdev ]]; then
		log_must zpool detach $TESTPOOL1 $detachvdev
	fi
	if [[ -n $removevdev ]]; then
		[[ -z $finalpool ]] &&
		    log_fail "Must provide final pool status!"
		log_must zpool remove $TESTPOOL1 $removevdev
		log_must wait_for_pool_config $TESTPOOL1 "$finalpool"
	fi
	if [[ -n $pathstochange ]]; then
		#
		# Change device paths and re-import pool to update labels
		#
		zpool export $TESTPOOL1
		for dev in $pathstochange; do
			log_must mv $dev "${dev}_new"
			poolcheck=$(echo "$poolcheck" | \
			    sed "s:$dev:${dev}_new:g")
		done
		zpool import -d $DEVICE_DIR $TESTPOOL1
	fi

	#
	# In an attempt to leave MOS data untouched so extreme
	# rewind is successful during import we checkpoint the
	# pool and hope that these MOS data are part of the
	# checkpoint (e.g they stay around). If this goes as
	# expected, then extreme rewind should rewind back even
	# further than the time that we took the checkpoint.
	#
	# Note that, ideally we would want to take a checkpoint
	# right after we record the txg we plan to rewind to.
	# But since we can't attach, detach or remove devices
	# while having a checkpoint, we take it after the
	# operation that changes the config.
	#
	# However, it is possible the MOS data was overwritten
	# in which case the pool will either be unimportable, or
	# may have been rewound prior to the data being written.
	# In which case an error is returned and test_common()
	# is retried by the caller to minimize false positives.
	#
	log_must zpool checkpoint $TESTPOOL1

	log_must overwrite_data $TESTPOOL1 ""

	log_must zpool export $TESTPOOL1

	if zpool import -d $DEVICE_DIR -T $txg $TESTPOOL1; then
		verify_data_hashsums $MD5FILE && retval=0

		log_must check_pool_config $TESTPOOL1 "$poolcheck"
		log_must zpool destroy $TESTPOOL1
	fi

	# Cleanup
	if [[ -n $pathstochange ]]; then
		for dev in $pathstochange; do
			log_must mv "${dev}_new" $dev
		done
	fi
	# Fast way to clear vdev labels
	log_must zpool create -f $TESTPOOL2 $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4
	log_must zpool destroy $TESTPOOL2

	log_note ""
	return $retval
}

function test_add_vdevs
{
	typeset poolcreate="$1"
	typeset addvdevs="$2"

	log_note "$0: pool '$poolcreate', add $addvdevs."

	for retry in $(seq 1 5); do
		test_common "$poolcreate" "$addvdevs" && return
		log_note "Retry $retry / 5 for test_add_vdevs()"
	done

	log_fail "Exhausted all 5 retries for test_add_vdevs()"
}

function test_attach_vdev
{
	typeset poolcreate="$1"
	typeset attachto="$2"
	typeset attachvdev="$3"

	log_note "$0: pool '$poolcreate', attach $attachvdev to $attachto."

	for retry in $(seq 1 5); do
		test_common "$poolcreate" "" "$attachto $attachvdev" && return
		log_note "Retry $retry / 5 for test_attach_vdev()"
	done

	log_fail "Exhausted all 5 retries for test_attach_vdev()"
}

function test_detach_vdev
{
	typeset poolcreate="$1"
	typeset detachvdev="$2"

	log_note "$0: pool '$poolcreate', detach $detachvdev."

	for retry in $(seq 1 5); do
		test_common "$poolcreate" "" "" "$detachvdev" && return
		log_note "Retry $retry / 5 for test_detach_vdev()"
	done

	log_fail "Exhausted all 5 retries for test_detach_vdev()"
}

function test_attach_detach_vdev
{
	typeset poolcreate="$1"
	typeset attachto="$2"
	typeset attachvdev="$3"
	typeset detachvdev="$4"

	log_note "$0: pool '$poolcreate', attach $attachvdev to $attachto," \
	    "then detach $detachvdev."

	for retry in $(seq 1 5); do
		test_common "$poolcreate" "" "$attachto $attachvdev" \
		    "$detachvdev" && return
		log_note "Retry $retry / 5 for test_attach_detach_vdev()"
	done

	log_fail "Exhausted all 5 retries for test_attach_detach_vdev()"
}

function test_remove_vdev
{
	typeset poolcreate="$1"
	typeset removevdev="$2"
	typeset finalpool="$3"

	log_note "$0: pool '$poolcreate', remove $removevdev."

	for retry in $(seq 1 5); do
		test_common "$poolcreate" "" "" "" "$removevdev" \
		    "$finalpool" && return
		log_note "Retry $retry / 5 for test_remove_vdev()"
	done

	log_fail "Exhausted all 5 retries for test_remove_vdev()"
}

# Record txg history
is_linux && log_must set_tunable32 TXG_HISTORY 100

# Make the devices bigger to reduce chances of overwriting MOS metadata.
increase_device_sizes $(( FILE_SIZE * 4 ))

# Increase the number of metaslabs for small pools temporarily to
# reduce the chance of reusing a metaslab that holds old MOS metadata.
log_must set_tunable64 VDEV_MIN_MS_COUNT 150

# Part of the rewind test is to see how it reacts to path changes
typeset pathstochange="$VDEV0 $VDEV1 $VDEV2 $VDEV3"

log_note " == test rewind after device addition == "

test_add_vdevs "$VDEV0" "$VDEV1"
test_add_vdevs "$VDEV0 $VDEV1" "$VDEV2"
test_add_vdevs "$VDEV0" "$VDEV1 $VDEV2"
test_add_vdevs "mirror $VDEV0 $VDEV1" "mirror $VDEV2 $VDEV3"
test_add_vdevs "$VDEV0" "raidz $VDEV1 $VDEV2 $VDEV3"
test_add_vdevs "$VDEV0" "draid $VDEV1 $VDEV2 $VDEV3"
test_add_vdevs "$VDEV0" "log $VDEV1"
test_add_vdevs "$VDEV0 log $VDEV1" "$VDEV2"

log_note " == test rewind after device attach == "

test_attach_vdev "$VDEV0" "$VDEV0" "$VDEV1"
test_attach_vdev "mirror $VDEV0 $VDEV1" "$VDEV0" "$VDEV2"
test_attach_vdev "$VDEV0 $VDEV1" "$VDEV0" "$VDEV2"

log_note " == test rewind after device removal == "

# Once we remove a device it will be overlooked in the device scan, so we must
# preserve its original path
pathstochange="$VDEV0 $VDEV2"
test_remove_vdev "$VDEV0 $VDEV1 $VDEV2" "$VDEV1" "$VDEV0 $VDEV2"

#
# Path change and detach are incompatible. Detach changes the guid of the vdev
# so we have no direct way to link the new path to an existing vdev.
#
pathstochange=""

log_note " == test rewind after device detach == "

test_detach_vdev "mirror $VDEV0 $VDEV1" "$VDEV1"
test_detach_vdev "mirror $VDEV0 $VDEV1 mirror $VDEV2 $VDEV3" "$VDEV1"
test_detach_vdev "$VDEV0 log mirror $VDEV1 $VDEV2" "$VDEV2"

log_note " == test rewind after device attach followed by device detach == "

#
# We need to disable vdev validation since once we detach VDEV1, VDEV0 will
# inherit the mirror tvd's guid and lose its original guid.
#
set_vdev_validate_skip 1
test_attach_detach_vdev "$VDEV0" "$VDEV0" "$VDEV1" "$VDEV1"
set_vdev_validate_skip 0

log_pass "zpool import rewind after configuration change passed."
