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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026, Showta Ishizaki.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify a zvol accepts write-zeroes requests and frees the range,
#	rather than having the kernel write zero-filled data.
#
# STRATEGY:
# 1. Check the zvol advertises write_zeroes_max_bytes
# 2. Write 5MB of random data to the zvol
# 3. Zero 1MB at offset 0 with BLKZEROOUT (blkdiscard -z), which the
#    kernel sends as WRITE_ZEROES with REQ_NOUNMAP
# 4. Zero 1MB at an offset not aligned to volblocksize, so the partial
#    blocks at each end must be zeroed in place
# 5. Punch a 512KB hole at offset 4.5MB (fallocate -p), which the
#    kernel sends as WRITE_ZEROES without a data fallback
# 6. Observe the freed space and verify the ranges read back as zeroes
#

verify_runnable "global"

typeset datafile1="$(mktemp -t zvol_misc_wz1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_wz2.XXXXXX)"
typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

function cleanup
{
	rm -f "$datafile1" "$datafile2"
}

function do_test
{
	typeset MB=1048576
	typeset vbs=$(get_prop volblocksize $TESTPOOL/$TESTVOL)
	typeset unaligned=$((3 * MB + 4096))

	block_device_wait $zvolpath

	typeset dev=$(basename $(readlink -f $zvolpath))
	typeset wzmax=$(cat /sys/block/$dev/queue/write_zeroes_max_bytes)
	log_note "write_zeroes_max_bytes=$wzmax volblocksize=$vbs"
	log_must test "$wzmax" -gt 0

	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=5
	log_must dd if="$datafile1" of=$zvolpath conv=fsync
	sync_pool
	log_must within_tolerance $(get_prop refer $TESTPOOL/$TESTVOL) \
	    $((5 * MB)) 131072

	log_must $zerocmd -o 0 -l $MB $zvolpath
	log_must $zerocmd -o $unaligned -l $MB $zvolpath
	log_must fallocate -p -o $((4 * MB + MB / 2)) -l $((MB / 2)) $zvolpath
	sync_pool

	# Only the whole volblocksize blocks inside the unaligned range are
	# freed; the block at each end keeps its zeroed remainder.
	typeset freed=$((MB + (MB - vbs) + MB / 2))
	log_must within_tolerance $(get_prop refer $TESTPOOL/$TESTVOL) \
	    $((5 * MB - freed)) 131072

	log_must dd if=/dev/zero of="$datafile1" bs=1M count=1 conv=notrunc
	log_must dd if=/dev/zero of="$datafile1" bs=4k count=256 \
	    seek=$((unaligned / 4096)) conv=notrunc
	log_must dd if=/dev/zero of="$datafile1" bs=512k count=1 seek=9 \
	    conv=notrunc
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=5
	log_must cmp "$datafile1" "$datafile2"
}

log_assert "Verify that a ZFS volume frees space on write-zeroes requests"
log_onexit cleanup

if blkdiscard --help | grep -q '\-\-force' ; then
	trimcmd='blkdiscard --force'
else
	trimcmd='blkdiscard'
fi
zerocmd="$trimcmd -z"

log_must zfs set compression=off $TESTPOOL/$TESTVOL
log_must $trimcmd $zvolpath

set_blk_mq 1
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

set_blk_mq 0
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

log_pass "ZFS volumes free space on write-zeroes requests"
