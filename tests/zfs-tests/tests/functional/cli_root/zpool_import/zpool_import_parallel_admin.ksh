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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2023 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
# 	Verify that admin commands to different pool are not blocked by import
#
# STRATEGY:
#	1. Create 2 pools
#	2. Export one of the pools
#	4. Import the pool with an injected delay
#	5. Execute some admin commands against both pools
#	6. Verify that the admin commands to the non-imported pool don't stall
#

verify_runnable "global"

function cleanup
{
	zinject -c all
	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL2
}

function pool_import
{
	typeset dir=$1
	typeset pool=$2

	SECONDS=0
	errmsg=$(zpool import -d $dir -f $pool 2>&1 > /dev/null)
	if [[ $? -eq 0 ]]; then
		echo ${pool}: imported in $SECONDS secs
		echo $SECONDS > ${DEVICE_DIR}/${pool}-import
	else
		echo ${pool}: import failed $errmsg in $SECONDS secs
	fi
}

function pool_add_device
{
	typeset pool=$1
	typeset device=$2
	typeset devtype=$3

	SECONDS=0
	errmsg=$(zpool add $pool $devtype $device 2>&1 > /dev/null)
	if [[ $? -eq 0 ]]; then
		echo ${pool}: added $devtype vdev in $SECONDS secs
		echo $SECONDS > ${DEVICE_DIR}/${pool}-add
	else
		echo ${pool}: add $devtype vdev failed ${errmsg}, in $SECONDS secs
	fi
}

function pool_stats
{
	typeset stats=$1
	typeset pool=$2

	SECONDS=0
	errmsg=$(zpool $stats $pool 2>&1 > /dev/null)
	if [[ $? -eq 0 ]]; then
		echo ${pool}: $stats in $SECONDS secs
		echo $SECONDS > ${DEVICE_DIR}/${pool}-${stats}
	else
		echo ${pool}: $stats failed ${errmsg}, in $SECONDS secs
	fi
}

function pool_create
{
	typeset pool=$1
	typeset device=$2

	SECONDS=0
	errmsg=$(zpool create $pool $device 2>&1 > /dev/null)
	if [[ $? -eq 0 ]]; then
		echo ${pool}: created in $SECONDS secs
		echo $SECONDS > ${DEVICE_DIR}/${pool}-create
	else
		echo ${pool}: create failed ${errmsg}, in $SECONDS secs
	fi
}

log_assert "Simple admin commands to different pool not blocked by import"

log_onexit cleanup

#
# create two pools and export one
#
log_must zpool create $TESTPOOL1 $VDEV0
log_must zpool export $TESTPOOL1
log_must zpool create $TESTPOOL2 $VDEV1

#
# import pool asyncronously with an injected 10 second delay
#
log_must zinject -P import -s 10 $TESTPOOL1
pool_import $DEVICE_DIR $TESTPOOL1 &

sleep 2

#
# run some admin commands on the pools while the import is in progress
#

pool_add_device $TESTPOOL1 $VDEV2 "log" &
pool_add_device $TESTPOOL2 $VDEV3 "cache" &
pool_stats "status" $TESTPOOL1 &
pool_stats "status" $TESTPOOL2 &
pool_stats "list" $TESTPOOL1 &
pool_stats "list" $TESTPOOL2 &
pool_create $TESTPOOL1 $VDEV4 &
wait

log_must zpool sync $TESTPOOL1 $TESTPOOL2

zpool history $TESTPOOL1
zpool history $TESTPOOL2

log_must test "5" -lt $(<${DEVICE_DIR}/${TESTPOOL1}-import)

#
# verify that commands to second pool did not wait for import to finish
#
log_must test "2" -gt $(<${DEVICE_DIR}/${TESTPOOL2}-status)
log_must test "2" -gt $(<${DEVICE_DIR}/${TESTPOOL2}-list)
log_must test "2" -gt $(<${DEVICE_DIR}/${TESTPOOL2}-add)
[[ -e ${DEVICE_DIR}/${TESTPOOL1}-create ]] && log_fail "unexpected pool create"

log_pass "Simple admin commands to different pool not blocked by import"
