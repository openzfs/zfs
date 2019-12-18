#!/bin/ksh -p

#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Intel Corporation. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Automated Auto-Spare Test when
# drive is faulted due to CHECKSUM ERRORS.
#
# STRATEGY:
# 1. Create a pool with hot spares
# 2. Create a filesystem with the primary cache disable to force reads
# 3. Write a file to the pool to be read back
# 4. Inject CHECKSUM ERRORS on read with a zinject error handler
# 5. Verify the ZED kicks in a hot spare and expected pool/device status
# 6. Clear the fault
# 7. Verify the hot spare is available and expected pool/device status
#

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	rm -f $VDEV_FILES $SPARE_FILE
}

log_assert "Testing automated auto-spare FMA test"

log_onexit cleanup

# Events not supported on FreeBSD
if ! is_freebsd; then
	# Clear events from previous runs
	zed_events_drain
fi

TESTFILE="/$TESTPOOL/$TESTFS/testfile"

for type in "mirror" "raidz" "raidz2"; do
	# 1. Create a pool with hot spares
	truncate -s $SPA_MINDEVSIZE $VDEV_FILES $SPARE_FILE
	log_must zpool create -f $TESTPOOL $type $VDEV_FILES spare $SPARE_FILE

	# 2. Create a filesystem with the primary cache disable to force reads
	log_must zfs create -o primarycache=none $TESTPOOL/$TESTFS
	log_must zfs set recordsize=16k $TESTPOOL/$TESTFS

	# 3. Write a file to the pool to be read back
	log_must dd if=/dev/urandom of=$TESTFILE bs=1M count=16

	# 4. Inject CHECKSUM ERRORS on read with a zinject error handler
	log_must zinject -d $FAULT_FILE -e corrupt -f 50 -T read $TESTPOOL
	log_must cp $TESTFILE /dev/null

	# 5. Verify the ZED kicks in a hot spare and expected pool/device status
	log_note "Wait for ZED to auto-spare"
	log_must wait_vdev_state $TESTPOOL $FAULT_FILE "DEGRADED" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_FILE "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_FILE "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 6. Clear the fault
	log_must zinject -c all
	log_must zpool clear $TESTPOOL $FAULT_FILE

	# 7. Verify the hot spare is available and expected pool/device status
	log_must wait_vdev_state $TESTPOOL $FAULT_FILE "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_FILE "AVAIL"
	log_must check_state $TESTPOOL "" "ONLINE"

	cleanup
done

log_pass "Auto-spare test successful"
