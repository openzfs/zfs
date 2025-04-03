#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2022, Klara Inc.
#

# DESCRIPTION:
#	Verify that vdev properties, checksum_n and checksum_t, work with ZED.
#
# STRATEGY:
#	1. Create a pool with single vdev
#	2. Set checksum_n/checksum_t to non-default values
#	3. Inject checksum errors
#	4. Verify that ZED degrades vdev
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

MOUNTDIR="$TEST_BASE_DIR/checksum_mount"
FILEPATH="$MOUNTDIR/checksum_file"
VDEV="$TEST_BASE_DIR/vdevfile.$$"
POOL="checksum_pool"
FILESIZE="10M"

function cleanup
{
	log_must zed_stop

	log_must zinject -c all
	if poolexists $POOL ; then
		destroy_pool $POOL
	fi
	log_must rm -fd $VDEV $MOUNTDIR
}

log_onexit cleanup

log_assert "Test ZED checksum_N and checksum_T configurability"

function do_setup
{
	log_must zpool create -f -m $MOUNTDIR $POOL $VDEV
	log_must zpool events -c
	log_must truncate -s 0 $ZED_DEBUG_LOG
	log_must zfs set compression=off $POOL
	log_must zfs set primarycache=none $POOL
	log_must zfs set recordsize=512 $POOL
}

function do_clean
{
	log_must zinject -c all
	log_must zpool destroy $POOL
}

function must_degrade
{
	log_must wait_vdev_state $POOL $VDEV "DEGRADED" 60
}

function mustnot_degrade
{
	log_must file_wait $ZED_DEBUG_LOG 5
	log_must wait_vdev_state $POOL $VDEV "ONLINE" 60
}

# Test default settings of ZED:
#   checksum_n=10
#   checksum_t=600
# fire 10 events, should degrade.
function default_degrade
{
	do_setup

	log_must mkfile $FILESIZE $FILEPATH
	log_must zinject -a -t data -e checksum -T read -f 100 $FILEPATH

	blk=0
	for _ in {1..10}; do
		dd if=$FILEPATH of=/dev/null bs=1 count=1 skip=$blk 2>/dev/null
		blk=$((blk+512))
	done

	must_degrade

	do_clean
}

# Set checksum_t=1
# fire 10 events over 2.5 seconds, should not degrade.
function checksum_t_no_degrade
{
	do_setup

	log_must zpool set checksum_t=1 $POOL $VDEV
	log_must mkfile $FILESIZE $FILEPATH
	log_must zinject -a -t data -e checksum -T read -f 100 $FILEPATH

	blk=0
	for _ in {1..10}; do
		dd if=$FILEPATH of=/dev/null bs=1 count=1 skip=$blk 2>/dev/null
		blk=$((blk+512))
		sleep 0.25
	done

	mustnot_degrade

	do_clean
}

# Set checksum_n=1
# fire 1 event, should degrade.
function checksum_n_degrade
{
	do_setup

	log_must zpool set checksum_n=1 $POOL $VDEV
	log_must mkfile $FILESIZE $FILEPATH
	log_must zinject -a -t data -e checksum -T read -f 100 $FILEPATH

	dd if=$FILEPATH of=/dev/null bs=1 count=1 2>/dev/null

	must_degrade

	do_clean
}

log_must truncate -s $MINVDEVSIZE $VDEV
log_must mkdir -p $MOUNTDIR

log_must zed_start
default_degrade
checksum_n_degrade
checksum_t_no_degrade

log_pass "Test ZED checksum_N and checksum_T configurability"
