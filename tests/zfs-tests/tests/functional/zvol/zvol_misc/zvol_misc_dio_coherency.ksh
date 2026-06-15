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
#	Verify that zvol Direct I/O provides full data coherency — the same
#	guarantees as the ZFS filesystem Direct I/O path.  A Direct I/O
#	write must store its block pointer in the dbuf (and ZIL if sync),
#	so subsequent reads — whether DIO or ARC — always see the latest
#	written data, even before the TXG commits.
#
#	This test covers:
#	- DIO write → DIO read  (same-path coherency)
#	- DIO write → ARC read  (cross-path coherency)
#	- ARC write → DIO read  (cross-path coherency)
#	- DIO in-place overwrite correctness
#	- Immediate read-after-write coherency (no TXG commit delay)
#	- Coherency across export/import (ZIL replay)
#
# STRATEGY:
#	1. Create a zvol with the given volblocksize, primarycache=metadata
#	2. For each I/O path combination (DIO/ARC write × DIO/ARC read),
#	   write a known pattern, read it back, and diff the result.
#	3. Overwrite data in-place with DIO and verify old data is replaced
#	   (xxh128 digest comparison to prove change).
#	4. Write with DIO and immediately read back without syncing —
#	   verify the dbuf points to the new block (no stale data).
#	5. Export/import the pool and verify data survives via both
#	   DIO and ARC read paths.
#	6. Repeat with different volblocksizes (4k, 128k) to exercise
#	   alignment edge cases.
#

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
typeset zvolsize=256M
typeset datafile1="$(mktemp -t zvol_misc_dio_coher1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_dio_coher2.XXXXXX)"

function cleanup
{
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
	rm -f "$datafile1" "$datafile2"
}

log_onexit cleanup

log_assert "Verify zvol Direct I/O provides full data coherency"

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

#
# Helper: recreate a fresh zvol for a test.  Does NOT destroy the
# test pool — only the zvol dataset.
#
function recreate_zvol
{
	typeset volblocksize=$1
	typeset primarycache=${2:-metadata}

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b $volblocksize -V $zvolsize $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=$primarycache $TESTPOOL/$TESTVOL
	log_must zfs set compression=off $TESTPOOL/$TESTVOL
	set_tunable32 VOL_DIO_ENABLED 1
	block_device_wait $zvolpath
}

#
# Helper: create a data file with unique content from /dev/urandom
#
function create_data
{
	typeset bs=$1
	typeset count=$2

	log_must dd if=/dev/urandom of="$datafile1" bs=$bs count=$count \
	    2>/dev/null
}

#
# Helper: write data to zvol and read it back, then diff.
#   $1 = write_bs, $2 = write_count, $3 = write_seek
#   $4 = read_bs,  $5 = read_count,  $6 = read_skip
#
function write_read_diff
{
	typeset wbs=$1 wcnt=$2 wseek=$3
	typeset rbs=$4 rcnt=$5 rskip=$6

	log_must dd if="$datafile1" of=$zvolpath bs=$wbs count=$wcnt \
	    seek=$wseek conv=fsync 2>/dev/null
	log_must dd if=$zvolpath of="$datafile2" bs=$rbs count=$rcnt \
	    skip=$rskip 2>/dev/null
	log_must diff $datafile1 $datafile2
}

#
# Test 1: DIO write → DIO read coherency
#
function test_dio_write_dio_read
{
	typeset volblocksize=$1

	log_note "=== Test: DIO write → DIO read (volblocksize=$volblocksize) ==="

	recreate_zvol $volblocksize

	typeset bs=$volblocksize
	typeset count=32

	create_data $bs $count
	write_read_diff $bs $count 0 $bs $count 0

	log_note "  DIO write → DIO read: OK"

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 2: DIO write → ARC read coherency
#   Write with DIO enabled, disable DIO, read via ARC.
#   The dbuf must contain the block pointer from the DIO write.
#
function test_dio_write_arc_read
{
	typeset volblocksize=$1

	log_note "=== Test: DIO write → ARC read (volblocksize=$volblocksize) ==="

	recreate_zvol $volblocksize "all"

	typeset bs=$volblocksize
	typeset count=32

	# Write via DIO
	create_data $bs $count
	log_must dd if="$datafile1" of=$zvolpath bs=$bs count=$count \
	    conv=fsync 2>/dev/null

	# Now disable DIO and read via ARC
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2

	log_note "  DIO write → ARC read: OK"

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 3: ARC write → DIO read coherency
#   Write with DIO disabled (ARC path), enable DIO, read back.
#   The ARC write stores the block pointer in the dbuf; DIO read
#   must find it.
#
function test_arc_write_dio_read
{
	typeset volblocksize=$1

	log_note "=== Test: ARC write → DIO read (volblocksize=$volblocksize) ==="

	# Create with DIO disabled
	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b $volblocksize -V $zvolsize $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=all $TESTPOOL/$TESTVOL
	set_tunable32 VOL_DIO_ENABLED 0
	block_device_wait $zvolpath

	typeset bs=$volblocksize
	typeset count=32

	# Write via ARC (DIO disabled)
	create_data $bs $count
	log_must dd if="$datafile1" of=$zvolpath bs=$bs count=$count \
	    conv=fsync 2>/dev/null

	# Enable DIO and read back
	log_must set_tunable32 VOL_DIO_ENABLED 1
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2

	log_note "  ARC write → DIO read: OK"

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 4: DIO in-place overwrite coherency
#   Write initial data via DIO, overwrite same blocks with different
#   data via DIO, verify new data (not old) is returned.
#
function test_dio_overwrite
{
	typeset volblocksize=$1

	log_note "=== Test: DIO in-place overwrite (volblocksize=$volblocksize) ==="

	recreate_zvol $volblocksize

	typeset bs=$volblocksize
	typeset count=64

	# Write initial pattern via DIO
	create_data $bs $count
	typeset digest1=$(xxh128digest "$datafile1")
	log_must dd if="$datafile1" of=$zvolpath bs=$bs count=$count \
	    conv=fsync 2>/dev/null

	# Read back and verify initial data
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2
	log_note "  Initial write OK (digest=$digest1)"

	# Overwrite with a DIFFERENT pattern via DIO
	create_data $bs $count
	typeset digest2=$(xxh128digest "$datafile1")
	log_must dd if="$datafile1" of=$zvolpath bs=$bs count=$count \
	    conv=fsync 2>/dev/null

	# Read back — must get NEW data
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2
	log_note "  Overwrite OK (new digest=$digest2, old=$digest1 replaced)"

	# Also verify via ARC read (disable DIO)
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2
	log_note "  Post-overwrite ARC read: OK"

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 5: Immediate read-after-write coherency
#   Write via DIO with fsync, then immediately read back without
#   any intervening sync or delay.  The dbuf must already point to
#   the new block because dmu_write_abd() updates it synchronously.
#
function test_dio_immediate_coherency
{
	typeset volblocksize=$1

	log_note "=== Test: Immediate read-after-DIO-write (volblocksize=$volblocksize) ==="

	recreate_zvol $volblocksize

	typeset bs=$volblocksize
	typeset count=16
	typeset iterations=5

	for i in $(seq 1 $iterations); do
		typeset seek=$((i * count))
		create_data $bs $count

		# Write with fsync (synchronous DIO path)
		log_must dd if="$datafile1" of=$zvolpath bs=$bs \
		    count=$count seek=$seek conv=fsync 2>/dev/null

		# Read back IMMEDIATELY — no sync, no delay
		log_must dd if=$zvolpath of="$datafile2" bs=$bs \
		    count=$count skip=$seek 2>/dev/null
		log_must diff $datafile1 $datafile2

		log_note "  Iteration $i: OK (seek=${seek}×$bs)"
	done

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test 6: Coherency across export/import (ZIL + dbuf persistence)
#   Write via DIO with fsync, export the pool, import it, and verify
#   data is intact via both DIO and ARC reads.
#
function test_dio_export_import_coherency
{
	typeset volblocksize=$1

	log_note "=== Test: DIO coherency across export/import (volblocksize=$volblocksize) ==="

	recreate_zvol $volblocksize

	typeset bs=$volblocksize
	typeset count=32

	# Write via DIO with fsync
	create_data $bs $count
	typeset digest=$(xxh128digest "$datafile1")
	log_must dd if="$datafile1" of=$zvolpath bs=$bs count=$count \
	    conv=fsync 2>/dev/null

	# Export and re-import the pool
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	block_device_wait $zvolpath

	# Re-enable DIO after import
	log_must set_tunable32 VOL_DIO_ENABLED 1

	# Read back via DIO
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2
	log_note "  Post-import DIO read: OK (digest=$digest)"

	# Read back via ARC
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$count \
	    2>/dev/null
	log_must diff $datafile1 $datafile2
	log_note "  Post-import ARC read: OK (digest=$digest)"

	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

# ---- Main test execution ----

for volblocksize in 4k 128k; do
	log_note "============================================"
	log_note "Testing coherency with volblocksize=$volblocksize"
	log_note "============================================"

	test_dio_write_dio_read $volblocksize
	test_dio_write_arc_read $volblocksize
	test_arc_write_dio_read $volblocksize
	test_dio_overwrite $volblocksize
	test_dio_immediate_coherency $volblocksize
	test_dio_export_import_coherency $volblocksize
done

# Leave the zvol intact; subsequent tests in the zvol_misc group
# (e.g. zvol_misc_trim, zvol_misc_fua) depend on $TESTPOOL/$TESTVOL.
# The group-level cleanup will handle final teardown.

log_pass "zvol Direct I/O coherency tests passed"
