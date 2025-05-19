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
# AnyRAID mirror can resilver a disk after it gets back online.
#
# STRATEGY:
# 1. Offline one disk.
# 2. Write to the pool.
# 3. Get that disk back online.
# 4. Get it resilvered.
#

verify_runnable "global"

log_assert "AnyRAID mirror can resilver a disk after it gets back online"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

# anyraid1

log_must create_sparse_files "disk" 3 $DEVSIZE
log_must zpool create -f $TESTPOOL anyraid1 $disks

log_must zpool offline $TESTPOOL $disk0
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must dd if=/dev/urandom of=/$TESTPOOL/file.bin bs=1M count=128
log_must zpool online $TESTPOOL $disk0
log_must check_state $TESTPOOL $disk0 "online"
for i in {1..60}; do
	check_state $TESTPOOL "" "online" && break
	sleep 1
done
zpool status
log_must check_state $TESTPOOL "" "online"

log_must destroy_pool $TESTPOOL


# anyraid2

log_must create_sparse_files "disk" 5 $DEVSIZE
log_must zpool create -f $TESTPOOL anyraid2 $disks

log_must zpool offline $TESTPOOL $disk0
log_must zpool offline $TESTPOOL $disk1
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL $disk1 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must dd if=/dev/urandom of=/$TESTPOOL/file.bin bs=1M count=128
log_must zpool online $TESTPOOL $disk0
log_must zpool online $TESTPOOL $disk1
log_must check_state $TESTPOOL $disk0 "online"
log_must check_state $TESTPOOL $disk1 "online"
for i in {1..60}; do
	check_state $TESTPOOL "" "online" && break
	sleep 1
done
zpool status
log_must check_state $TESTPOOL "" "online"

log_must destroy_pool $TESTPOOL


# anyraid3

log_must create_sparse_files "disk" 7 $DEVSIZE
log_must zpool create -f $TESTPOOL anyraid3 $disks

log_must zpool offline $TESTPOOL $disk0
log_must zpool offline $TESTPOOL $disk1
log_must zpool offline $TESTPOOL $disk2
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL $disk1 "offline"
log_must check_state $TESTPOOL $disk2 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must dd if=/dev/urandom of=/$TESTPOOL/file.bin bs=1M count=128
log_must zpool online $TESTPOOL $disk0
log_must zpool online $TESTPOOL $disk1
log_must zpool online $TESTPOOL $disk2
log_must check_state $TESTPOOL $disk0 "online"
log_must check_state $TESTPOOL $disk1 "online"
log_must check_state $TESTPOOL $disk2 "online"
for i in {1..60}; do
	check_state $TESTPOOL "" "online" && break
	sleep 1
done
zpool status
log_must check_state $TESTPOOL "" "online"

log_must destroy_pool $TESTPOOL

log_pass "AnyRAID mirror can resilver a disk after it gets back online"
