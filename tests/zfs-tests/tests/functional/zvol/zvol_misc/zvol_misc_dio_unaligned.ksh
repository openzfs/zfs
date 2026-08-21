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
#	Verify that unaligned zvol I/O correctly falls back to the ARC
#	path when Direct I/O is enabled. Also verify that aligned I/O
#	goes through the DIO path.
#
# STRATEGY:
#	1. Create a zvol with primarycache=metadata
#	2. Enable DIO, do unaligned writes/reads, verify data integrity
#	3. Do aligned writes/reads, verify data integrity
#	4. Verify both paths produce correct data
#	5. Repeat with different volblocksizes
#

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset datafile1="$(mktemp -t zvol_misc_dio_unalign1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_dio_unalign2.XXXXXX)"
typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

function cleanup
{
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
	rm -f "$datafile1" "$datafile2"
}

log_onexit cleanup

log_assert "Verify unaligned zvol I/O falls back to ARC when DIO is enabled"

#
# Test unaligned writes/reads with DIO enabled
#
function test_dio_unaligned
{
	typeset volblocksize=$1

	log_note "Testing unaligned I/O with volblocksize=$volblocksize"

	# Recreate the zvol with the specified volblocksize.
	# Only destroy/recreate the zvol — do NOT destroy the test pool,
	# as subsequent tests in the same group depend on it.
	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b $volblocksize -V 256M $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Test sub-blocksize writes (should go through ARC, not DIO)
	# These are too small and unaligned for DIO
	typeset small_bs=512
	log_note "  Sub-blocksize unaligned writes ($small_bs bytes)"
	log_must dd if=/dev/urandom of="$datafile1" bs=$small_bs count=1024
	log_must dd if=$datafile1 of=$zvolpath bs=$small_bs count=1024 \
	    conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=$small_bs count=1024
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Test offset-misaligned writes using fio.
	# fio's --offset option allows placing a single volblocksize-sized
	# I/O at an exact byte offset that is not a multiple of
	# volblocksize.  With DIO enabled, the kernel must reject this
	# because the start offset is misaligned, and fall back to the
	# ARC path.  fio's built-in verify ensures data integrity on
	# read-back.  psync is the portable ioengine (Linux + FreeBSD).
	if command -v fio > /dev/null; then
		log_note "  Offset-misaligned writes (at offset volblocksize+512)"

		# Convert volblocksize to bytes for fio --offset
		typeset vbs_bytes
		case $volblocksize in
			*k|*K) vbs_bytes=$((${volblocksize%[kK]} * 1024)) ;;
			*m|*M) vbs_bytes=$((${volblocksize%[mM]} * 1048576)) ;;
			*g|*G) vbs_bytes=$((${volblocksize%[gG]} * 1073741824)) ;;
			*)     vbs_bytes=$volblocksize ;;
		esac
		typeset misalign_off=$((vbs_bytes + 512))

		# Write one volblocksize of data at offset (vbs_bytes + 512)
		log_must fio --filename=$zvolpath --name=misalign-write \
		    --rw=write --bs=$volblocksize --size=$volblocksize \
		    --offset=$misalign_off --ioengine=psync --iodepth=1 \
		    --verify=crc32c --do_verify=0 \
		    --verify_state_save=0 --group_reporting

		# Read back and verify data integrity
		log_must fio --filename=$zvolpath --name=misalign-read \
		    --rw=read --bs=$volblocksize --size=$volblocksize \
		    --offset=$misalign_off --ioengine=psync --iodepth=1 \
		    --verify=crc32c --do_verify=1 \
		    --verify_state_save=0 --group_reporting
	else
		log_note "  Skipping offset-misaligned test (fio not found)"
	fi

	# Test block-aligned writes (should go through DIO)
	log_note "  Block-aligned writes ($volblocksize bytes)"
	log_must dd if=/dev/urandom of="$datafile1" bs=$volblocksize count=8
	log_must dd if=$datafile1 of=$zvolpath bs=$volblocksize count=8 \
	    conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=$volblocksize count=8
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Destroy the zvol but keep the pool intact for subsequent tests.
	block_device_wait
	log_must zfs destroy $TESTPOOL/$TESTVOL
}

#
# Test that data written via ARC path is readable via DIO path and vice versa
#
function test_dio_mixed_paths
{
	log_note "Testing mixed DIO/ARC I/O paths"

	# Recreate the zvol for mixed-path testing.
	# Only destroy/recreate the zvol — do NOT destroy the test pool.
	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL
	log_must set_tunable32 VOL_DIO_ENABLED 1

	block_device_wait $zvolpath

	# Write via DIO
	log_note "  Write via DIO, read via ARC"
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 \
	    conv=fsync
	# Read back via ARC (no direct flag)
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Write via ARC, read via DIO
	log_note "  Write via ARC, read via DIO"
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=64
	# Set DIO off for the write
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=64 conv=fsync
	# Set DIO on for the read
	log_must set_tunable32 VOL_DIO_ENABLED 1
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=64
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Leave the zvol intact; subsequent tests in the zvol_misc group
	# (e.g. zvol_misc_trim, zvol_misc_fua) depend on $TESTPOOL/$TESTVOL.
	# The group-level cleanup will handle final teardown.
}

# ---- Main test execution ----

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED

test_dio_unaligned 8k
test_dio_unaligned 128k
test_dio_mixed_paths

log_pass "Unaligned zvol I/O correctly falls back to ARC when DIO is enabled"
