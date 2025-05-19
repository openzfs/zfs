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

# anymirror1

log_must create_sparse_files "disk" 3 $DEVSIZE
log_must zpool create -f $TESTPOOL anymirror1 $disks

log_must zpool offline $TESTPOOL $disk0
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 128 -d R
log_must zpool online $TESTPOOL $disk0
log_must check_state $TESTPOOL $disk0 "online"
for i in {1..60}; do
	check_state $TESTPOOL "" "online" && break
	sleep 1
done
zpool status
log_must check_state $TESTPOOL "" "online"

log_must destroy_pool $TESTPOOL


# anymirror2

log_must create_sparse_files "disk" 5 $DEVSIZE
log_must zpool create -f $TESTPOOL anymirror2 $disks

log_must zpool offline $TESTPOOL $disk0
log_must zpool offline $TESTPOOL $disk1
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL $disk1 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 128 -d R
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


# anymirror3

log_must create_sparse_files "disk" 7 $DEVSIZE
log_must zpool create -f $TESTPOOL anymirror3 $disks

log_must zpool offline $TESTPOOL $disk0
log_must zpool offline $TESTPOOL $disk1
log_must zpool offline $TESTPOOL $disk2
log_must check_state $TESTPOOL $disk0 "offline"
log_must check_state $TESTPOOL $disk1 "offline"
log_must check_state $TESTPOOL $disk2 "offline"
log_must check_state $TESTPOOL "" "degraded"

log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 128 -d R
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
