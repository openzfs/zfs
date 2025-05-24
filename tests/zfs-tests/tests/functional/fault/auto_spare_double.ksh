#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
# Try do induce a double spare condition and verify we're prevented from doing
# it.
#
# STRATEGY:
# 1. Fail a drive
# 2. Kick in first spare
# 3. Bring new drive back online and start resilvering to it
# 4. Immediately after the resilver starts, fail the new drive.
# 5. Try to kick in the second spare for the new drive (which is now failed)
# 6. Verify that we can't kick in the second spare
#
# Repeat this test for both traditional spares and dRAID spares.
#
. $STF_SUITE/include/libtest.shlib

LAST_VDEV=3
SIZE_MB=300

ZED_PID="$(zed_check)"

function cleanup
{
	destroy_pool $TESTPOOL
	for i in {0..$LAST_VDEV} ; do
		log_must rm -f $TEST_BASE_DIR/file$i
	done

	# Restore ZED if it was running before this test
	if [ -n $ZED_PID ] ; then
		log_must zed_start
	fi
}

log_assert "Cannot attach two spares to same failed vdev"
log_onexit cleanup

# Stop ZED if it's running
if [ -n $ZED_PID ] ; then
	log_must zed_stop
fi

log_must truncate -s ${SIZE_MB}M $TEST_BASE_DIR/file{0..$LAST_VDEV}

ZFS_DBGMSG=/proc/spl/kstat/zfs/dbgmsg

# Run the test - we assume the pool is already created.
# $1: disk to fail
# $2: 1st spare name
# $3: 2nd spare name
function do_test {
	FAIL_DRIVE=$1
	SPARE0=$2
	SPARE1=$3
	echo 0 > $ZFS_DBGMSG
	log_must zpool status

	log_note "Kicking in first spare ($SPARE0)"
	log_must zpool offline -f $TESTPOOL $FAIL_DRIVE
	log_must zpool replace $TESTPOOL $FAIL_DRIVE $SPARE0

	# Fill the pool with data to make the resilver take a little
	# time.
	dd if=/dev/zero of=/$TESTPOOL/testfile bs=1M || true

	# Zero our failed disk.  It will appear as a blank.
	rm -f $FAIL_DRIVE
	truncate -s ${SIZE_MB}M $FAIL_DRIVE

	# Attempt to replace our failed disk, then immediately fault it.
	log_must zpool replace $TESTPOOL $FAIL_DRIVE
	log_must zpool offline -f $TESTPOOL $FAIL_DRIVE
	log_must check_state $TESTPOOL $FAIL_DRIVE "faulted"

	log_note "Kicking in second spare ($SPARE0)... This should not work..."
	log_mustnot zpool replace $TESTPOOL $FAIL_DRIVE $SPARE1
	# Verify the debug line in dbgmsg
	log_must grep 'disk would create double spares' $ZFS_DBGMSG

	# Disk should still be faulted
	log_must check_state $TESTPOOL $FAIL_DRIVE "faulted"
}

# Test with traditional spares
log_must zpool create -O compression=off -O recordsize=4k -O primarycache=none \
	$TESTPOOL mirror $TEST_BASE_DIR/file{0,1} spare $TEST_BASE_DIR/file{2,3}
do_test $TEST_BASE_DIR/file1 $TEST_BASE_DIR/file2 $TEST_BASE_DIR/file3
destroy_pool $TESTPOOL

# Clear vdev files for next test
for i in {0..$LAST_VDEV} ; do
	log_must rm -f $TEST_BASE_DIR/file$i
done
log_must truncate -s ${SIZE_MB}M $TEST_BASE_DIR/file{0..$LAST_VDEV}

# Test with dRAID spares
log_must zpool create -O compression=off -O recordsize=4k -O primarycache=none \
	$TESTPOOL draid1:1d:4c:2s $TEST_BASE_DIR/file{0..3}
do_test $TEST_BASE_DIR/file1 draid1-0-0 draid1-0-1

log_pass "Verified we cannot attach two spares to same failed vdev"
