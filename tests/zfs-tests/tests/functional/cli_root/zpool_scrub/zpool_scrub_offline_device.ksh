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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_reopen/zpool_reopen.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
# Scrubbing a pool with offline devices correctly preserves DTL entries
#
# STRATEGY:
# 1. Create the pool
# 2. Offline the first device
# 3. Write to the pool
# 4. Scrub the pool
# 5. Online the first device and offline the second device
# 6. Scrub the pool again
# 7. Verify data integrity
#
# NOTE:
# Ported from script used to reproduce issue #5806
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -f $DISK1 $DISK2 $DISK3 $DISK4
}

#
# Update to [online|offline] $device status on $pool synchronously
#
function zpool_do_sync # <status> <pool> <device>
{
	status="$1"
	pool="$2"
	device="$3"

	if [[ $status != "online" && $status != "offline" ]]; then
		log_fail "zpool_do_sync: invalid status $status"
	fi

	log_must zpool $status $pool $device
	for i in {1..10}; do
		check_state $pool $device $status && return 0
	done
	log_fail "Failed to $status device $device"
}

#
# Start a scrub on $pool and wait for its completion
#
function zpool_scrub_sync # <pool>
{
	pool="$1"

	log_must zpool scrub $pool
	while ! is_pool_scrubbed $pool; do
		sleep 1
	done
}

log_assert "Scrubbing a pool with offline devices correctly preserves DTLs"
log_onexit cleanup

DEVSIZE='128m'
FILESIZE='100m'
TESTDIR="$TEST_BASE_DIR/zpool_scrub_offline_device"
DISK1="$TEST_BASE_DIR/zpool_disk1.dat"
DISK2="$TEST_BASE_DIR/zpool_disk2.dat"
DISK3="$TEST_BASE_DIR/zpool_disk3.dat"
DISK4="$TEST_BASE_DIR/zpool_disk4.dat"
RESILVER_TIMEOUT=40

# 1. Create the pool
log_must truncate -s $DEVSIZE $DISK1
log_must truncate -s $DEVSIZE $DISK2
log_must truncate -s $DEVSIZE $DISK3
log_must truncate -s $DEVSIZE $DISK4
poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
log_must zpool create -O mountpoint=$TESTDIR $TESTPOOL2 \
    raidz2 $DISK1 $DISK2 $DISK3 $DISK4

# 2. Offline the first device
zpool_do_sync 'offline' $TESTPOOL2 $DISK1

# 3. Write to the pool
log_must mkfile $FILESIZE "$TESTDIR/data.bin"

# 4. Scrub the pool
zpool_scrub_sync $TESTPOOL2

# 5. Online the first device and offline the second device
zpool_do_sync 'online' $TESTPOOL2 $DISK1
zpool_do_sync 'offline' $TESTPOOL2 $DISK2
log_must wait_for_resilver_end $TESTPOOL2 $RESILVER_TIMEOUT

# 6. Scrub the pool again
zpool_scrub_sync $TESTPOOL2

# 7. Verify data integrity
cksum=$(zpool status $TESTPOOL2 | awk 'L{print $NF;L=0} /CKSUM$/{L=1}')
if [[ $cksum != 0 ]]; then
	log_fail "Unexpected CKSUM errors found on $TESTPOOL2 ($cksum)"
fi

log_pass "Scrubbing a pool with offline devices correctly preserves DTLs"
