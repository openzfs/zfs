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
# Copyright (c) 2023, Klara Inc.
#

# DESCRIPTION:
#	Verify that vdev properties, slow_io_n and slow_io_t, work with ZED.
#
# STRATEGY:
#	1. Create a pool with single vdev
#	2. Set slow_io_n/slow_io_t to non-default values
#	3. Inject slow io errors
#	4. Verify that ZED degrades vdev
#

. $STF_SUITE/include/libtest.shlib

TESTDIR="$TEST_BASE_DIR/zed_slow_io"
VDEV="$TEST_BASE_DIR/vdevfile.$$"
TESTPOOL="slow_io_pool"
FILEPATH="$TESTDIR/slow_io.testfile"

OLD_SLOW_IO=$(get_tunable ZIO_SLOW_IO_MS)
OLD_SLOW_IO_EVENTS=$(get_tunable SLOW_IO_EVENTS_PER_SECOND)

verify_runnable "both"

function do_setup
{
	log_must truncate -s 1G $VDEV
	default_setup_noexit $VDEV
	zed_events_drain
	log_must zfs set compression=off $TESTPOOL
	log_must zfs set primarycache=none $TESTPOOL
	log_must zfs set prefetch=none $TESTPOOL
	log_must zfs set recordsize=512 $TESTPOOL
	for i in {1..10}; do
		dd if=/dev/urandom of=${FILEPATH}$i bs=512 count=1 2>/dev/null
	done
	zpool sync
}

# intermediate cleanup
function do_clean
{
	log_must zinject -c all
	log_must zpool destroy $TESTPOOL
	log_must rm -f $VDEV
}

# final cleanup
function cleanup
{
	log_must zinject -c all

	# if pool still exists then something failed so log additional info
	if poolexists $TESTPOOL ; then
		log_note "$(zpool status -s $TESTPOOL)"
		echo "=================== zed log search ==================="
		grep "Diagnosis Engine" $ZEDLET_DIR/zed.log
		destroy_pool $TESTPOOL
	fi
	log_must zed_stop

	log_must rm -f $VDEV

	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS
}

function start_slow_io
{
	zpool sync
	log_must set_tunable64 ZIO_SLOW_IO_MS 10
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND 1000

	log_must zinject -d $VDEV -D10:1 -T read $TESTPOOL
	zpool sync
}

function stop_slow_io
{
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS

	log_must zinject -c all
}

# Test default ZED settings:
#    inject 10 events over 2.5 seconds, should not degrade.
function default_degrade
{
	do_setup

	start_slow_io
	for i in {1..10}; do
		dd if=${FILEPATH}$i of=/dev/null count=1 bs=512 2>/dev/null
		sleep 0.25
	done
	stop_slow_io
	log_note "$(zpool status -s $TESTPOOL)"

	# give slow ZED a chance to process the delay events
	sleep 18
	log_note "$(zpool status -s $TESTPOOL)"

	degrades=$(grep "zpool_vdev_degrade" $ZEDLET_DIR/zed.log | wc -l)
	log_note $degrades vdev degrades in ZED log
	[ $degrades -eq "0" ] || \
		log_fail "expecting no degrade events, found $degrades"

	do_clean
}

# change slow_io_n, slow_io_t to 5 events in 60 seconds
# fire more than 5 events, should degrade
function slow_io_degrade
{
	do_setup

	zpool set slow_io_n=5 $TESTPOOL $VDEV
	zpool set slow_io_t=60 $TESTPOOL $VDEV

	start_slow_io
	for i in {1..16}; do
		dd if=${FILEPATH}$i of=/dev/null count=1 bs=512 2>/dev/null
		sleep 0.5
	done
	stop_slow_io
	zpool sync

	#
	# wait up to 60 seconds for kernel to produce at least 5 delay events
	#
	typeset -i i=0
	typeset -i events=0
	while [[ $i -lt 60 ]]; do
		events=$(zpool events | grep "ereport\.fs\.zfs.delay" | wc -l)
		[[ $events -ge "5" ]] && break
		i=$((i+1))
		sleep 1
	done
	log_note "$events delay events found"

	if [[ $events -ge "5" ]]; then
		log_must wait_vdev_state $TESTPOOL $VDEV "DEGRADED" 10
	fi

	do_clean
}

# change slow_io_n, slow_io_t to 10 events in 1 second
# inject events spaced 0.5 seconds apart, should not degrade
function slow_io_no_degrade
{
	do_setup

	zpool set slow_io_n=10 $TESTPOOL $VDEV
	zpool set slow_io_t=1 $TESTPOOL $VDEV

	start_slow_io
	for i in {1..16}; do
		dd if=${FILEPATH}$i of=/dev/null count=1 bs=512 2>/dev/null
		sleep 0.5
	done
	stop_slow_io
	zpool sync

	log_mustnot wait_vdev_state $TESTPOOL $VDEV "DEGRADED" 45

	do_clean
}

log_assert "Test ZED slow io configurability"
log_onexit cleanup

log_must zed_events_drain
log_must zed_start

default_degrade
slow_io_degrade
slow_io_no_degrade

log_pass "Test ZED slow io configurability"
