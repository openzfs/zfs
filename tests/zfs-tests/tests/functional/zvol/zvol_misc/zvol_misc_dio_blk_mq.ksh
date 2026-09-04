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
#	Linux-specific: Verify that zvol Direct I/O works correctly with
#	both the legacy bio path and the blk-mq (block multiqueue) path.
#	On Linux, the ZVOL DIO code must handle both 'struct bio' and
#	'struct request' (blk-mq) paths, extracting page pointers from
#	different structures: bio_vec vs rq_for_each_segment.
#
# STRATEGY:
#	1. Create a zvol
#	2. Test DIO with blk-mq disabled (legacy bio path)
#	3. Test DIO with blk-mq enabled (request path)
#	4. Verify data integrity in both modes
#	5. Test with DIO enabled/disabled in both modes
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "Linux-specific blk-mq test"
fi

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
typeset datafile1="$(mktemp -t zvol_misc_dio_blkmq1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_dio_blkmq2.XXXXXX)"

function cleanup
{
	if tunable_exists VOL_DIO_ENABLED ; then
		set_tunable32 VOL_DIO_ENABLED 0
		rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
	fi
	rm -f "$datafile1" "$datafile2"
	# Reset blk-mq to default (enabled on modern kernels)
	if tunable_exists VOL_USE_BLK_MQ ; then
		set_tunable32 VOL_USE_BLK_MQ 1
	fi
}

log_onexit cleanup

log_assert "Verify zvol DIO works correctly with blk-mq on and off (Linux)"

# Clean up any stale saved tunable from a previous crashed run
rm -f $TEST_BASE_DIR/tunable-VOL_DIO_ENABLED
rm -f $TEST_BASE_DIR/tunable-VOL_USE_BLK_MQ

#
# Main test function — runs DIO write/read/verify under a given blk-mq mode
#
function test_dio_blk_mq_mode
{
	typeset blkmq=$1  # 0 = legacy bio, 1 = blk-mq
	typeset label=$2

	log_note "=== Test: DIO with blk-mq=$blkmq ($label) ==="

	log_must set_tunable32 VOL_DIO_ENABLED 0
	if datasetexists $TESTPOOL/$TESTVOL; then
		log_must zfs destroy $TESTPOOL/$TESTVOL
	fi
	block_device_wait
	log_must zfs create -b 128k -V 256M $TESTPOOL/$TESTVOL
	log_must zfs set primarycache=metadata $TESTPOOL/$TESTVOL

	# Set blk-mq mode
	if tunable_exists VOL_USE_BLK_MQ ; then
		log_must set_tunable32 VOL_USE_BLK_MQ $blkmq
		# Need export/import for blk-mq change to take effect
		log_must zpool export $TESTPOOL
		log_must zpool import $TESTPOOL
	else
		log_note "VOL_USE_BLK_MQ not available (single-queue kernel)"
	fi

	log_must set_tunable32 VOL_DIO_ENABLED 1
	block_device_wait $zvolpath

	# Test writes at various sizes
	for bs in 4k 128k 1M; do
		typeset cnt=$((128 / ${bs%[kM]}))
		[[ $bs == "4k" ]] && cnt=32768
		[[ $bs == "128k" ]] && cnt=1024
		[[ $bs == "1M" ]] && cnt=128

		log_note "  DIO write/read bs=$bs cnt=$cnt"
		log_must dd if=/dev/urandom of="$datafile1" bs=$bs count=$cnt
		log_must dd if=$datafile1 of=$zvolpath bs=$bs count=$cnt \
		    conv=fsync
		log_must dd if=$zvolpath of="$datafile2" bs=$bs count=$cnt
		log_must diff $datafile1 $datafile2
		log_must rm -f "$datafile1" "$datafile2"
	done

	# Also test with sync writes
	log_note "  DIO sync write/read"
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=32
	log_must dd if=$datafile1 of=$zvolpath bs=1M count=32 \
	    conv=fsync
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=32
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Verify ECKSUM fallback: disable DIO, dirty ARC with known data,
	# then read back with DIO (which may hit ARC-cached data)
	log_note "  DIO read with ARC-cached data"
	log_must set_tunable32 VOL_DIO_ENABLED 0
	log_must dd if=/dev/urandom of="$datafile1" bs=128k count=128
	# Write via ARC path to populate cache
	log_must dd if=$datafile1 of=$zvolpath bs=128k count=128 conv=fsync
	log_must dd if=$zvolpath of=/dev/null bs=128k count=128
	# Now read via DIO path — data should be correct regardless of
	# whether it came from ARC cache or disk
	log_must set_tunable32 VOL_DIO_ENABLED 1
	log_must dd if=$zvolpath of="$datafile2" bs=128k count=128
	log_must diff $datafile1 $datafile2
	log_must rm -f "$datafile1" "$datafile2"

	# Leave the zvol intact; subsequent tests in the linux.run group
	# (e.g. zvol_misc_dio_stress) depend on $TESTPOOL/$TESTVOL.
	# The group-level cleanup will handle final teardown.
}

# ---- Main test execution ----

# Test with blk-mq off (legacy bio path)
test_dio_blk_mq_mode 0 "legacy-bio"

# Test with blk-mq on
test_dio_blk_mq_mode 1 "blk-mq"

log_pass "zvol DIO works correctly with blk-mq on and off (Linux)"
