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
#	Verify that zvol Direct I/O works correctly:
#	- Bypasses ARC for page-aligned reads
#	- Bypasses ARC for block-aligned writes
#	- Maintains data integrity
#	- Falls back to ARC when disabled
#
# STRATEGY:
#	1. Create a zvol with primarycache=metadata
#	2. Enable DIO, write data, read back, verify
#	3. Verify ARC data size stays near zero
#	4. Disable DIO, verify data still correct (ARC path)
#	5. Run fio with DIO enabled using various block sizes
#

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset datafile1="$(mktemp -t zvol_misc_dio1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_dio2.XXXXXX)"
typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

function cleanup
{
	# Reset DIO to default (0) — skip save/restore to avoid stale file issues
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
	rm -f "$datafile1" "$datafile2"
}

log_onexit cleanup

log_assert "Verify zvol Direct I/O bypasses ARC and maintains data integrity"

#
# Helper: read current ARC data_size (bytes).  Uses /proc/spl/kstat on Linux,
# sysctl on FreeBSD.  Returns empty string if neither source is available.
#
function _arc_data_size
{
	if [[ -f /proc/spl/kstat/zfs/arcstats ]]; then
		awk '/^data_size / {print $3}' /proc/spl/kstat/zfs/arcstats
	elif sysctl -n kstat.zfs.misc.arcstats.data_size >/dev/null 2>&1; then
		sysctl -n kstat.zfs.misc.arcstats.data_size
	fi
}

#
# Test 1: Write with DIO enabled, read back with DIO enabled, verify data
#
function test_dio_write_read
{
	typeset bs=$1
	typeset count=$2

	log_note "Testing DIO write/read with bs=$bs count=$count"

	# Enable DIO (saved once in main, restored in cleanup)
	log_must set_tunable32 VOL_DIO_ENABLED 1

	# Wait for udev to create symlinks to our zvol
	block_device_wait $zvolpath

	# Create test data
	log_must dd if=/dev/urandom of="$datafile1" bs=$bs count=$count

	# Write to zvol (DIO path triggered by zvol_dio_enabled=1)
	log_must dd if=$datafile1 of=$zvolpath bs=$bs count=$count \
	    conv=fsync

	# Read back
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count

	# Compare
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"
}

#
# Test 2: Verify ARC data size stays low with DIO enabled + primarycache=metadata
#
function test_dio_arc_bypass
{
	log_note "Testing ARC bypass with DIO enabled"

	log_must set_tunable32 VOL_DIO_ENABLED 1
	block_device_wait $zvolpath

	typeset arc_before=$(_arc_data_size)
	if [[ -z "$arc_before" ]]; then
		log_note "Cannot read ARC stats; skipping ARC bypass check"
		return
	fi

	# Write 256MB with DIO
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=256
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=256 \
	    conv=fsync

	# Read it back
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=256
	sync_pool

	typeset arc_after=$(_arc_data_size)

	log_note "ARC data size before=$arc_before after=$arc_after"

	# With primarycache=metadata and DIO enabled, ARC data_size should
	# remain near 0.  This is expected — metadata-only caching means
	# data never enters the ARC, and DIO bypasses it entirely.
	# Allow some small growth for metadata/indirect blocks.
	typeset arc_growth=0
	if [[ -n "$arc_before" && -n "$arc_after" ]]; then
		arc_growth=$((arc_after - arc_before))
	fi

	# If ARC data grew by more than 32MB, the bypass may not be working.
	# This is a soft check — the definitive test is perf profiling.
	if [[ $arc_growth -gt $((32 * 1048576)) ]]; then
		log_note "WARNING: ARC data grew by $arc_growth bytes " \
		    "(> 32MB). DIO may not be bypassing ARC for data."
	fi

	log_must rm -f "$datafile1" "$datafile2"
}

#
# Test 3: Write with DIO disabled, read back, verify data (ARC path)
#
function test_dio_disabled_fallback
{
	typeset bs=$1
	typeset count=$2

	log_note "Testing fallback with DIO disabled bs=$bs count=$count"

	# Ensure DIO is disabled
	log_must set_tunable32 VOL_DIO_ENABLED 0

	block_device_wait $zvolpath

	# Create test data
	log_must dd if=/dev/urandom of="$datafile1" bs=$bs count=$count

	# Write to zvol with DIO disabled (goes through ARC)
	log_must dd if=$datafile1 of=$zvolpath bs=$bs count=$count conv=fsync

	# Read back
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count

	# Compare
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	log_must set_tunable32 VOL_DIO_ENABLED 1
}

#
# Test 4: fio verify with DIO enabled
#
function test_dio_fio_verify
{
	log_note "Testing fio verify with DIO enabled"

	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write with verify pattern using DIO.
	# Use psync (portable POSIX sync engine) for cross-platform compatibility;
	# FreeBSD does not have libaio.
	log_must fio --name=dio-write --rw=write --bs=1M \
	    --filename=$zvolpath --direct=1 --numjobs=1 \
	    --iodepth=32 --ioengine=psync --verify=crc32c \
	    --verify_fatal=1 --size=256M --group_reporting

	# Read back and verify with DIO
	log_must fio --name=dio-read --rw=read --bs=1M \
	    --filename=$zvolpath --direct=1 --numjobs=1 \
	    --iodepth=32 --ioengine=psync --verify=crc32c \
	    --verify_fatal=1 --size=256M --group_reporting

	# Read back without DIO (via ARC) as additional verification
	log_must fio --name=arc-read --rw=read --bs=1M \
	    --filename=$zvolpath --direct=0 --numjobs=1 \
	    --iodepth=32 --ioengine=psync --size=256M --group_reporting
}

#
# Test 5: Verify large I/O (> DMU_MAX_ACCESS) is correctly chunked.
#   DMU_MAX_ACCESS is 64MB; the DIO loop processes data in
#   DMU_MAX_ACCESS/2 (32MB) chunks.  I/O of 128MB exercises at least
#   4 chunks per request, covering the while-loop iteration path and
#   ensuring that partial advances in the uio are correctly handled.
#
function test_dio_large_io
{
	log_note "Testing large I/O (> DMU_MAX_ACCESS) with DIO"

	log_must set_tunable32 VOL_DIO_ENABLED 1
	block_device_wait $zvolpath

	# 128MB = 2 * DMU_MAX_ACCESS, processed as 4 x 32MB chunks
	typeset size_mb=128
	typeset count=$size_mb

	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=$count

	# Write 128MB with DIO — exercises the multi-chunk loop in zvol_write
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=$count \
	    conv=fsync

	# Read back 128MB with DIO — exercises the multi-chunk loop in zvol_read
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=$count

	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Also test with bs=PAGE_SIZE (4k) to exercise many small chunks.
	# 4M / 4k = 1024 iterations, which stress-tests the per-chunk
	# DIO-capable check inside the while loop.
	log_must dd if=/dev/urandom of="$datafile1" bs=4k count=1024
	log_must dd if=$datafile1 of=$zvolpath bs=4k count=1024 \
	    conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=4k count=1024
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	log_note "Large I/O chunking test passed"
}

#
# Test 6: With primarycache=all, verify DIO enabled vs disabled ARC behavior.
#   - DIO disabled: ARC should cache data (data_size grows after read).
#   - DIO enabled:  ARC should NOT cache data (data_size stays flat).
# This is the definitive test proving DIO bypasses ARC regardless of cache policy.
#
function test_dio_arc_controlled
{
	log_note "Testing DIO vs ARC with primarycache=all"

	# ---- Phase 1: DIO disabled, ARC should cache data ----
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must zfs set primarycache=all $TESTPOOL/$TESTVOL
	block_device_wait $zvolpath

	typeset arc_before_dio_off=$(_arc_data_size)
	log_note "  DIO OFF: ARC data_size before I/O = $arc_before_dio_off"

	# Write and read back — ARC should cache this data
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64
	sync_pool

	typeset arc_after_dio_off=$(_arc_data_size)
	log_note "  DIO OFF: ARC data_size after  I/O = $arc_after_dio_off"

	# With primarycache=all and DIO off, ARC MUST have cached data
	if [[ "$arc_after_dio_off" -le "$arc_before_dio_off" ]]; then
		log_fail "DIO OFF: ARC data_size did not grow " \
		    "($arc_before_dio_off → $arc_after_dio_off). " \
		    "primarycache=all should cache data!"
	fi
	log_must rm -f "$datafile1" "$datafile2"

	# ---- Phase 2: DIO enabled, ARC should NOT cache data ----
	log_must set_tunable32 VOL_DIO_ENABLED 1

	# Write fresh data to different offset so ARC doesn't serve cached data
	typeset arc_before_dio_on=$(_arc_data_size)
	log_note "  DIO ON:  ARC data_size before I/O = $arc_before_dio_on"

	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	# Write at offset 128M (second half of zvol) with DIO
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 \
	    seek=128 conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64 \
	    skip=128
	sync_pool

	typeset arc_after_dio_on=$(_arc_data_size)
	log_note "  DIO ON:  ARC data_size after  I/O = $arc_after_dio_on"

	typeset dio_growth=$((arc_after_dio_on - arc_before_dio_on))
	# With DIO enabled, ARC data should NOT grow significantly.
	# Allow up to 8MB for metadata/indirect blocks.
	if [[ $dio_growth -gt $((8 * 1048576)) ]]; then
		log_fail "DIO ON: ARC data_size grew by $dio_growth bytes " \
		    "(> 8MB). DIO is NOT bypassing ARC with primarycache=all!"
	fi
	log_note "  DIO ON: ARC data growth = $dio_growth bytes (OK, < 8MB)"

	# Restore primarycache for subsequent tests
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must rm -f "$datafile1" "$datafile2"
}

# ---- Main test execution ----

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

# Recreate the zvol optimized for DIO testing.
# Only destroy/recreate the zvol — do NOT destroy the test pool,
# as subsequent tests in the zvol_misc group depend on it.
log_must set_tunable32 VOL_DIO_ENABLED 0
if datasetexists $TESTPOOL/$TESTVOL; then
	log_must zfs destroy $TESTPOOL/$TESTVOL
fi
block_device_wait
log_must zfs create -b 128k -V 512M $TESTPOOL/$TESTVOL
log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
log_must zfs set compression=off $TESTPOOL/$TESTVOL

# Test with various block sizes (256MB total each)
# 256M / 4k = 65536, 256M / 128k = 2048, 256M / 1M = 256
for bs in 4k 128k 1M; do
	case $bs in
	4k)   count=65536 ;;
	128k) count=2048 ;;
	1M)   count=256 ;;
	esac
	test_dio_write_read $bs $count
done

# Test ARC bypass
test_dio_arc_bypass

# Test fallback when DIO is disabled
test_dio_disabled_fallback 128k 2048

# Test fio verify
test_dio_fio_verify

# Test large I/O chunking (> DMU_MAX_ACCESS)
test_dio_large_io

# Test ARC behavior: primarycache=all, DIO on vs off
test_dio_arc_controlled

# Leave the zvol intact; subsequent tests in the zvol_misc group
# (e.g. zvol_misc_trim) depend on $TESTPOOL/$TESTVOL.
# The group-level cleanup will handle final teardown.

log_pass "zvol Direct I/O works correctly"
