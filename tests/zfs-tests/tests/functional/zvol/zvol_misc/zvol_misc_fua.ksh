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
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

set_blk_mq 1
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

log_pass "ZFS volume FUA works"
