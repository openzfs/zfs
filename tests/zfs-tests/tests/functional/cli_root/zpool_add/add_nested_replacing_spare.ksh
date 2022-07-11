#!/bin/ksh -p
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

#
# DESCRIPTION:
#	'zpool add' works with nested replacing/spare vdevs
#
# STRATEGY:
#	1. Create a redundant pool with a spare device
#	2. Manually fault a device, wait for the hot-spare and then replace it:
#	   this creates a situation where replacing and spare vdevs are nested.
#	3. Verify 'zpool add' is able to add new devices to the pool.
#

verify_runnable "global"

function cleanup
{
	zed_stop
	zed_cleanup
	log_must zinject -c all
	destroy_pool $TESTPOOL
	log_must rm -f $DATA_DEVS $SPARE_DEVS
}

log_assert "'zpool add' works with nested replacing/spare vdevs"
log_onexit cleanup

FAULT_DEV="$TEST_BASE_DIR/fault-dev"
SAFE_DEV1="$TEST_BASE_DIR/safe-dev1"
SAFE_DEV2="$TEST_BASE_DIR/safe-dev2"
SAFE_DEV3="$TEST_BASE_DIR/safe-dev3"
SAFE_DEVS="$SAFE_DEV1 $SAFE_DEV2 $SAFE_DEV3"
REPLACE_DEV="$TEST_BASE_DIR/replace-dev"
ADD_DEV="$TEST_BASE_DIR/add-dev"
DATA_DEVS="$FAULT_DEV $SAFE_DEVS $REPLACE_DEV $ADD_DEV"
SPARE_DEV1="$TEST_BASE_DIR/spare-dev1"
SPARE_DEV2="$TEST_BASE_DIR/spare-dev2"
SPARE_DEVS="$SPARE_DEV1 $SPARE_DEV2"

# We need ZED running to work with spares
zed_setup
zed_start
# Clear events from previous runs
zed_events_drain

for type in "mirror" "raidz1" "raidz2" "raidz3"
do
	# 1. Create a redundant pool with a spare device
	truncate -s $SPA_MINDEVSIZE $DATA_DEVS $SPARE_DEVS
	log_must zpool create $TESTPOOL $type $FAULT_DEV $SAFE_DEVS
	log_must zpool add $TESTPOOL spare $SPARE_DEV1

	# 2.1 Fault a device, verify the spare is kicked in
	log_must zinject -d $FAULT_DEV -e nxio -T all -f 100 $TESTPOOL
	log_must zpool reopen $TESTPOOL
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV "UNAVAIL" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_DEV1 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV1 "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 2.2 Replace the faulted device: this creates a replacing vdev inside a
	#     spare vdev
	log_must zpool replace $TESTPOOL $FAULT_DEV $REPLACE_DEV
	log_must wait_vdev_state $TESTPOOL $REPLACE_DEV "ONLINE" 60
	zpool status | awk -v poolname="$TESTPOOL" -v type="$type" 'BEGIN {s=""}
	    $1 ~ poolname {c=4}; (c && c--) { s=s$1":" }
	    END { if (s != poolname":"type"-0:spare-0:replacing-0:") exit 1; }' ||
		log_fail "Pool does not contain nested replacing/spare vdevs"

	# 3. Verify 'zpool add' is able to add new devices
	log_must zpool add $TESTPOOL spare $SPARE_DEV2
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV2 "AVAIL"
	log_must zpool add -f $TESTPOOL $ADD_DEV
	log_must wait_vdev_state $TESTPOOL $ADD_DEV "ONLINE" 60

	# Cleanup
	log_must zinject -c all
	destroy_pool $TESTPOOL
	log_must rm -f $DATA_DEVS $SPARE_DEVS
done

log_pass "'zpool add' works with nested replacing/spare vdevs"
