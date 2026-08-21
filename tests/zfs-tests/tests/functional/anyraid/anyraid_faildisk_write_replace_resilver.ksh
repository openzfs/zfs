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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# AnyRAID mirror can resilver a replaced disk.
#
# STRATEGY:
# 1. Fail one disk.
# 2. Write new data to the pool.
# 3. Get that disk replaced and resilvered.
# 4. Repeat to verify sequential resilvering.
#

verify_runnable "global"

log_assert "AnyRAID mirror can resilver a replaced disk"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup


# anymirror1

for replace_flags in '' '-s'; do

	log_must create_sparse_files "disk" 3 $DEVSIZE
	log_must create_sparse_files "spare" 1 $DEVSIZE
	log_must zpool create -O compress=off -f $TESTPOOL anymirror1 $disks
	log_must zfs set primarycache=none $TESTPOOL

	# Write initial data
	log_must file_write -o create -f /$TESTPOOL/file1.bin -b 1048576 -c 256 -d Z

	# Fail one disk
	log_must truncate -s0 $disk0

	# Read initial data, write new data
	log_must dd if=/$TESTPOOL/file1.bin of=/dev/null bs=1M count=256
	log_must file_write -o create -f /$TESTPOOL/file1.bin -b 1048576 -c 256 -d Y

	# Check that disk is faulted
	zpool status
	log_must check_state $TESTPOOL $disk0 "faulted"

	# Initiate disk replacement
	log_must zpool replace -f $replace_flags $TESTPOOL $disk0 $spare0

	# Wait until resilvering is done and the pool is back online
	for i in {1..60}; do
		check_state $TESTPOOL "" "online" && break
		sleep 1
	done
	zpool status
	log_must check_state $TESTPOOL "" "online"

	destroy_pool $TESTPOOL

done

log_pass "AnyRAID mirror can resilver a replaced disk"
