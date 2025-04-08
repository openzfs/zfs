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
#	Verify that vdev properties, io_n and io_t, work with ZED.
#
# STRATEGY:
#	1. Create a mirrored pool.
#	3. Set io_n/io_t to non-default values
#	3. Inject io errors
#	4. Verify that ZED degrades vdev

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

MOUNTDIR="$TEST_BASE_DIR/io_mount"
FILEPATH="$MOUNTDIR/io_file"
VDEV="$TEST_BASE_DIR/vdevfile.$$"
VDEV1="$TEST_BASE_DIR/vdevfile1.$$"
POOL="io_pool"

function cleanup
{
	log_must zed_stop

	log_must zinject -c all
	if poolexists $POOL ; then
		destroy_pool $POOL
	fi
	log_must rm -fd $VDEV $VDEV1 $MOUNTDIR
	log_must set_tunable32 PREFETCH_DISABLE $zfsprefetch
}
log_onexit cleanup

log_assert "Test ZED io_n and io_t configurability"

zfsprefetch=$(get_tunable PREFETCH_DISABLE)
log_must set_tunable32 PREFETCH_DISABLE 1

function setup_pool
{
	log_must zpool create -f -m $MOUNTDIR $POOL mirror $VDEV $VDEV1
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

# Test default ZED settings:
#   io_n=10	(events)
#   io_t=600	(seconds)
# fire 10 events over 2.5 seconds, should degrade.
function default_degrade
{
	setup_pool

	log_must dd if=/dev/urandom of=$FILEPATH bs=1M count=64
	log_must zinject -a -d $VDEV -e io -T read -f 100 $POOL

	blk=0
	for _ in {1..10}; do
		dd if=$FILEPATH of=/dev/null bs=1 count=1 skip=$blk 2>/dev/null
		blk=$((blk+512))
		sleep 0.25
	done

	log_must wait_vdev_state $POOL $VDEV "FAULTED" 60
	do_clean
}

# set io_n=1
# fire 1 event, should degrade
function io_n_degrade
{
	setup_pool

	log_must zpool set io_n=1 $POOL $VDEV
	log_must dd if=/dev/urandom of=$FILEPATH bs=1M count=64
	log_must zinject -a -d $VDEV -e io -T read -f 100 $POOL

	dd if=$FILEPATH of=/dev/null bs=1 count=1 2>/dev/null

	log_must wait_vdev_state $POOL $VDEV "FAULTED" 60
	do_clean
}

# set io_t=1
# fire 10 events over 2.5 seconds, should not degrade
function io_t_nodegrade
{
	setup_pool

	log_must zpool set io_t=1 $POOL $VDEV
	log_must dd if=/dev/urandom of=$FILEPATH bs=1M count=64
	log_must zinject -a -d $VDEV -e io -T read -f 100 $POOL

	blk=0
	for _ in {1..10}; do
		dd if=$FILEPATH of=/dev/null bs=1 count=1 skip=$blk 2>/dev/null
		blk=$((blk+512))
		sleep 0.25
	done

	log_must file_wait $ZED_DEBUG_LOG 30
	log_must wait_vdev_state $POOL $VDEV "ONLINE" 1

	do_clean
}

log_must truncate -s $MINVDEVSIZE $VDEV
log_must truncate -s $MINVDEVSIZE $VDEV1
log_must mkdir -p $MOUNTDIR

log_must zed_start
default_degrade
io_n_degrade
io_t_nodegrade

log_pass "Test ZED io_n and io_t configurability"
