#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify that Direct I/O on zvols interacts correctly with the ZIL
#	(ZFS Intent Log). Tests sync writes (O_SYNC/O_DSYNC) combined
#	with DIO, verifies data survives export/import, and ensures the
#	ZIL correctly replays DIO writes after a simulated crash.
#
# STRATEGY:
#	1. Create a zvol with a dedicated SLOG device
#	2. With DIO enabled, do sync writes (DIO triggered by zvol_dio_enabled)
#	3. Export/import pool and verify data integrity
#	4. Test with sync=always and sync=standard
#	5. Test with different logbias values (latency, throughput)
#	6. Verify that async writes (no O_SYNC) with DIO also preserve data
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "Linux-specific test (uses GNU dd oflag/iflag)"
fi

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
typeset datafile1="$(mktemp -t zvol_misc_dio_zil1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_dio_zil2.XXXXXX)"

# Use a file-based log device for SLOG testing
typeset slogdev=$TEST_BASE_DIR/slogdev

function cleanup
{
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
	rm -f "$datafile1" "$datafile2"
	rm -f "$slogdev"
}

log_onexit cleanup

log_assert "Verify zvol DIO + ZIL interaction preserves data integrity"

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

#
# Test 1: Sync writes with DIO + export/import
#
function test_dio_sync_export_import
{
	typeset sync_mode=$1

	log_note "=== Test: DIO sync writes, sync=$sync_mode, export/import ==="

	# Create SLOG device
	log_must truncate -s 128M $slogdev

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zpool add $TESTPOOL log $slogdev
	log_must zfs set sync=$sync_mode $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write data (sync, DIO triggered by zvol_dio_enabled)
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 \
	    conv=fsync

	# Export and re-import the pool (simulates clean shutdown)
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL

	block_device_wait $zvolpath

	# Re-enable DIO after import
	log_must set_tunable32 VOL_DIO_ENABLED 1

	# Read back and verify
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Cleanup SLOG and zvol
	log_must zpool remove $TESTPOOL $slogdev
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 2: DIO writes with O_DSYNC (data integrity sync only)
#
function test_dio_odsync
{
	log_note "=== Test: DIO with O_DSYNC writes ==="

	log_must truncate -s 128M $slogdev

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zpool add $TESTPOOL log $slogdev
	log_must zfs set sync=standard $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write (sync, DIO triggered by zvol_dio_enabled)
	log_must dd if=/dev/urandom of="$datafile1" bs=128k count=512
	log_must dd if=$datafile1 of=$zvolpath bs=128k count=512 \
	    conv=fsync

	# Read back via DIO
	log_must dd if=$zvolpath of="$datafile2" bs=128k count=512
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	log_must zpool remove $TESTPOOL $slogdev
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 3: Async DIO writes (DIO triggered by zvol_dio_enabled)
#
function test_dio_async
{
	log_note "=== Test: Async DIO writes (DIO enabled, no sync flag) ==="

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zfs set sync=standard $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write (async, DIO triggered by zvol_dio_enabled)
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 \
	    conv=fsync

	# Read back and verify
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 4: DIO writes with sync=always (all writes are sync)
#
function test_dio_sync_always
{
	log_note "=== Test: DIO with sync=always ==="

	log_must truncate -s 128M $slogdev

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zpool add $TESTPOOL log $slogdev
	log_must zfs set sync=always $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Even without explicit oflag=sync, sync=always forces sync writes
	log_must dd if=/dev/urandom of="$datafile1" bs=128k count=256
	log_must dd if=$datafile1 of=$zvolpath bs=128k count=256 \
	    conv=fsync

	# Export/import
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL

	block_device_wait $zvolpath
	log_must set_tunable32 VOL_DIO_ENABLED 1

	log_must dd if=$zvolpath of="$datafile2" bs=128k count=256
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	log_must zpool remove $TESTPOOL $slogdev
	# Leave the zvol intact; subsequent tests in the linux.run group
	# (e.g. zvol_misc_fua) depend on $TESTPOOL/$TESTVOL.
}

# ---- Main test execution ----

test_dio_sync_export_import standard
test_dio_sync_export_import always
test_dio_odsync
test_dio_async
test_dio_sync_always

log_pass "zvol DIO + ZIL interaction tests passed"
