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
#	Stress-test zvol Direct I/O with concurrent mixed DIO/ARC I/O.
#	Verifies data integrity when multiple processes concurrently
#	read and write through both DIO and ARC paths to overlapping
#	regions. Also exercises I/O sizes near DMU_MAX_ACCESS.
#
# STRATEGY:
#	1. Create a zvol with primarycache=metadata
#	2. Enable DIO
#	3. Launch concurrent fio jobs mixing DIO and non-DIO I/O
#	4. Verify data integrity with fio's built-in verify
#	5. Test large block I/O (up to 16M)
#	6. Toggle DIO on/off mid-stream and verify no corruption
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "Linux-specific test (uses fio+libaio, GNU dd flags)"
fi

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
typeset zvolsize=512M

function cleanup
{
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
}

log_onexit cleanup

log_assert "Stress-test zvol DIO with concurrent mixed I/O paths"

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

#
# Test 1: Concurrent DIO and ARC readers/writers to overlapping regions
#
function test_dio_concurrent_mixed
{
	log_note "=== Test: Concurrent mixed DIO/ARC I/O ==="

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V $zvolsize $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write initial data via DIO
	log_must dd if=/dev/urandom of=$zvolpath bs=1M count=128 \
	    conv=fsync

	# Run concurrent DIO writes + DIO reads + ARC reads to same region
	# fio will use direct=1 for some jobs and direct=0 for others
	log_must fio --name=dio-write --rw=randwrite --bs=128k \
	    --filename=$zvolpath --direct=1 --numjobs=2 \
	    --iodepth=16 --ioengine=libaio --size=64M \
	    --verify=crc32c --verify_fatal=1 --do_verify=0 \
	    --offset=32M --time_based --runtime=30 --group_reporting \
	    --name=arc-read --rw=randread --bs=128k \
	    --filename=$zvolpath --direct=0 --numjobs=2 \
	    --iodepth=16 --ioengine=libaio --size=64M \
	    --offset=32M --time_based --runtime=30 --group_reporting \
	    --name=dio-read --rw=randread --bs=128k \
	    --filename=$zvolpath --direct=1 --numjobs=2 \
	    --iodepth=16 --ioengine=libaio --size=64M \
	    --offset=32M --time_based --runtime=30 --group_reporting

	# Verify the written data via DIO read
	log_must fio --name=dio-verify --rw=randread --bs=128k \
	    --filename=$zvolpath --direct=1 --numjobs=2 \
	    --iodepth=16 --ioengine=libaio --size=64M \
	    --verify=crc32c --verify_fatal=1 --offset=32M --group_reporting

	# Verify via ARC read as well
	log_must fio --name=arc-verify --rw=randread --bs=128k \
	    --filename=$zvolpath --direct=0 --numjobs=2 \
	    --iodepth=16 --ioengine=libaio --size=64M \
	    --verify=crc32c --verify_fatal=1 --offset=32M --group_reporting

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 2: Large block DIO (exercise I/O sizes near DMU_MAX_ACCESS)
#
function test_dio_large_blocks
{
	log_note "=== Test: Large block DIO (up to 16M) ==="

	log_must set_tunable32 VOL_DIO_ENABLED 0
	# Use large volblocksize to support large aligned I/Os
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 1M -V $zvolsize $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Test with 1M, 4M, 8M, 16M block sizes
	for bs in 1M 4M 8M 16M; do
		typeset cnt=$((256 / ${bs%M}))
		log_note "  DIO large write/read bs=$bs count=$cnt"
		log_must dd if=/dev/urandom of=$zvolpath bs=$bs count=$cnt \
		    conv=fsync
		log_must dd if=$zvolpath of=/dev/null bs=$bs count=$cnt
	done

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 3: Toggle DIO on/off while I/O is in flight
#
function test_dio_toggle_midstream
{
	log_note "=== Test: Toggle DIO on/off during I/O ==="

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V $zvolsize $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write initial 256M pattern
	log_must dd if=/dev/urandom of=$zvolpath bs=1M count=256 \
	    conv=fsync

	# Read back the full data while toggling DIO
	# Phase 1: DIO reads
	log_must dd if=$zvolpath of=/dev/null bs=1M count=64

	# Toggle DIO off mid-stream
	log_must set_tunable32 VOL_DIO_ENABLED 0
	# Phase 2: ARC reads (DIO disabled falls back to ARC)
	log_must dd if=$zvolpath of=/dev/null bs=1M count=64 skip=64

	# Toggle DIO back on
	log_must set_tunable32 VOL_DIO_ENABLED 1
	# Phase 3: DIO reads again
	log_must dd if=$zvolpath of=/dev/null bs=1M count=64 skip=128

	# Toggle DIO off
	log_must set_tunable32 VOL_DIO_ENABLED 0
	# Phase 4: ARC reads
	log_must dd if=$zvolpath of=/dev/null bs=1M count=64 skip=192

	# Final: read all data back and verify (DIO on for verification)
	log_must set_tunable32 VOL_DIO_ENABLED 1
	log_must dd if=/dev/urandom of=$zvolpath bs=1M count=256 \
	    conv=fsync
	# Re-write with a fixed pattern to compare
	typeset pattern="DIO_TOGGLE_TEST_PATTERN_0123456789"
	log_must dd if=/dev/zero of=$zvolpath bs=1M count=256 \
	    conv=fsync
	# Write the pattern at specific offset
	echo "$pattern" | dd of=$zvolpath bs=128k seek=512 conv=fsync
	# Read it back
	typeset readback=$(dd if=$zvolpath bs=128k skip=512 count=1 2>/dev/null)
	if [[ "$readback" != *"$pattern"* ]]; then
		log_fail "Data corruption after DIO toggle: expected '$pattern'"
	fi

	# Leave the zvol intact; subsequent tests in the linux.run group
	# (e.g. zvol_misc_dio_zil) depend on $TESTPOOL/$TESTVOL.
}

# ---- Main test execution ----

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

test_dio_concurrent_mixed
test_dio_large_blocks
test_dio_toggle_midstream

log_pass "zvol DIO stress tests passed"
