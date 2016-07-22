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
#	A pool should not try to write to a device that doesn't belong to it
#	anymore, even if the device is in its cachefile.
#
# STRATEGY:
#	1. Create pool1 with some devices and an alternate cachefile.
#	2. Backup the cachefile.
#	3. Export pool1.
#	4. Create pool2 using a device that belongs to pool1.
#	5. Export pool2.
#	6. Compute checksum of the shared device.
#	7. Import pool1 and write some data to it.
#	8. Verify that the checksum of the shared device hasn't changed.
#

verify_runnable "global"

function custom_cleanup
{
	destroy_pool $TESTPOOL2
	cleanup
}

log_onexit custom_cleanup

function dev_checksum
{
	typeset dev="$1"
	typeset checksum

	log_note "Compute checksum of '$dev'"

	checksum=$(md5sum $dev)
	if [[ $? -ne 0 ]]; then
		log_fail "Failed to compute checksum of '$dev'"
		return 1
	fi

	echo "$checksum"
	return 0
}

function test_shared_device
{
	typeset pool1="$1"
	typeset pool2="$2"
	typeset sharedvdev="$3"
	typeset importflags="${4:-}"

	log_note "$0: pool1 '$pool1', pool2 '$pool2' takes $sharedvdev."

	log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $pool1

	log_must cp $CPATH $CPATHBKP

	log_must zpool export $TESTPOOL1

	log_must zpool create -f $TESTPOOL2 $pool2

	log_must zpool export $TESTPOOL2

	typeset checksum1=$(dev_checksum $sharedvdev)

	log_must zpool import -c $CPATHBKP $importflags $TESTPOOL1

	log_must write_some_data $TESTPOOL1 2

	log_must zpool destroy $TESTPOOL1

	typeset checksum2=$(dev_checksum $sharedvdev)

	if [[ $checksum1 == $checksum2 ]]; then
		log_pos "Device hasn't been modified by original pool"
	else
		log_fail "Device has been modified by original pool." \
		    "Checksum mismatch: $checksum1 != $checksum2."
	fi

	# Cleanup
	log_must zpool import -d $DEVICE_DIR $TESTPOOL2
	log_must zpool destroy $TESTPOOL2
	log_must rm -f $CPATH $CPATHBKP

	log_note ""
}

test_shared_device "mirror $VDEV0 $VDEV1" "mirror $VDEV1 $VDEV2" "$VDEV1"
test_shared_device "mirror $VDEV0 $VDEV1 $VDEV2" "mirror $VDEV2 $VDEV3" \
    "$VDEV2"
test_shared_device "raidz $VDEV0 $VDEV1 $VDEV2" "$VDEV2" "$VDEV2"
test_shared_device "$VDEV0 log $VDEV1" "$VDEV2 log $VDEV1" "$VDEV1" "-m"

log_pass "Pool doesn't write to a device it doesn't own anymore."
