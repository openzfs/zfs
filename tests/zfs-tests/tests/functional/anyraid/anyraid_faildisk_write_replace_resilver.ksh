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


# anyraid1

for replace_flags in '' '-s'; do

	log_must create_sparse_files "disk" 3 $DEVSIZE
	log_must create_sparse_files "spare" 1 $DEVSIZE
	log_must zpool create -f $TESTPOOL anyraid1 $disks
	log_must zfs set primarycache=none $TESTPOOL

	# Write initial data
	log_must dd if=/dev/urandom of=/$TESTPOOL/file1.bin bs=1M count=$(( DEVSIZE / 2 / 1024 / 1024 ))

	# Fail one disk
	log_must truncate -s0 $disk0

	# Read initial data, write new data
	dd if=/$TESTPOOL/file1.bin of=/dev/null bs=1M count=$(( DEVSIZE / 2 / 1024 / 1024 ))
	log_must dd if=/dev/urandom of=/$TESTPOOL/file1.bin bs=1M count=$(( DEVSIZE / 2 / 1024 / 1024 ))

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
