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
# Copyright (c) 2022 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify that a zvol Force Unit Access (FUA) write works.
#
# STRATEGY:
# 1. dd write 5MB of data with "oflag=dsync,direct" to a zvol.  Those flags
#    together do a FUA write.
# 3. Verify the data is correct.
# 3. Repeat 1-2 for both the blk-mq and non-blk-mq cases.

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "This directory cannot be run on raw files."
fi

if ! is_linux ; then
	log_unsupported "Only linux supports dd with oflag=dsync for FUA writes"
fi

typeset datafile1="$(mktemp -t zvol_misc_fua1.XXXXXX)"
typeset datafile2="$(mktemp -t zvol_misc_fua2.XXXXXX)"
typeset datafile3="$(mktemp -t zvol_misc_fua3_log.XXXXXX)"
typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

typeset DISK1=${DISKS%% *}
function cleanup
{
	log_must zpool remove $TESTPOOL $datafile3
	rm "$datafile1" "$datafile2" "$datafile2"
}

# Prints the total number of sync writes for a vdev
# $1: vdev
function get_sync
{
	zpool iostat -p -H -v -r $TESTPOOL $1 | \
	    awk '/[0-9]+$/{s+=$4+$5} END{print s}'
}

function do_test {
	# Wait for udev to create symlinks to our zvol
	block_device_wait $zvolpath

	# Write using sync (creates FLUSH calls after writes, but not FUA)
	old_log_writes=$(get_sync $datafile3)

	log_must fio --name=write_iops --size=5M \
		--ioengine=libaio --verify=0 --bs=4K \
		--iodepth=1 --rw=randwrite --group_reporting=1 \
		--filename=$zvolpath --sync=1

	log_writes=$(( $(get_sync $datafile3) - $old_log_writes))

	# When doing sync writes, we should see at least one SLOG write per
	# block (5MB / 4KB) == 1280.
	log_note "Got $log_writes log writes."
	if [ $log_writes -lt 1280 ] ; then
		log_fail "Expected >= 1280 log writes. "
	fi

	# Create a data file
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=5

	# Write the data to our zvol using FUA
	log_must dd if=$datafile1 of=$zvolpath oflag=dsync,direct bs=1M count=5

	# Extract data from our zvol
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=5

	# Compare the data we expect with what's on our zvol.  diff will return
	# non-zero if they differ.
	log_must diff $datafile1 $datafile2

	log_must rm $datafile1 $datafile2
}

log_assert "Verify that a ZFS volume can do Force Unit Access (FUA)"
log_onexit cleanup

log_must zfs set compression=off $TESTPOOL/$TESTVOL
log_must truncate -s 100M $datafile3
log_must zpool add $TESTPOOL log $datafile3

log_note "Testing without blk-mq"

set_blk_mq 0
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

set_blk_mq 1
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

log_pass "ZFS volume FUA works"
