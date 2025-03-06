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
#	Verify that delay events from multiple vdevs doesn't degrade
#
# STRATEGY:
#	1. Create a pool with a 4 disk raidz vdev
#	2. Inject slow io errors
#	3. Verify that ZED detects slow I/Os but doesn't degrade any vdevs
#

. $STF_SUITE/include/libtest.shlib

TESTDIR="$TEST_BASE_DIR/zed_slow_io"
VDEV1="$TEST_BASE_DIR/vdevfile1.$$"
VDEV2="$TEST_BASE_DIR/vdevfile2.$$"
VDEV3="$TEST_BASE_DIR/vdevfile3.$$"
VDEV4="$TEST_BASE_DIR/vdevfile4.$$"
VDEVS="$VDEV1 $VDEV2 $VDEV3 $VDEV4"
TESTPOOL="slow_io_pool"
FILEPATH="$TESTDIR/slow_io.testfile"

OLD_SLOW_IO=$(get_tunable ZIO_SLOW_IO_MS)
OLD_SLOW_IO_EVENTS=$(get_tunable SLOW_IO_EVENTS_PER_SECOND)

verify_runnable "both"

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

	log_must rm -f $VDEVS
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS
}

function start_slow_io
{
	for vdev in $VDEVS
	do
		log_must zpool set slow_io_n=4 $TESTPOOL $vdev
		log_must zpool set slow_io_t=60 $TESTPOOL $vdev
	done
	zpool sync

	log_must set_tunable64 ZIO_SLOW_IO_MS 10
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND 1000

	for vdev in $VDEVS
	do
		log_must zinject -d $vdev -D10:1 $TESTPOOL
	done
	zpool sync
}

function stop_slow_io
{
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS

	log_must zinject -c all
}

function multiple_slow_vdevs_test
{
	log_must truncate -s 1G $VDEVS
	default_raidz_setup_noexit $VDEVS

	log_must zpool events -c
	log_must zfs set compression=off $TESTPOOL
	log_must zfs set primarycache=none $TESTPOOL
	log_must zfs set recordsize=4K $TESTPOOL

	log_must dd if=/dev/urandom of=$FILEPATH bs=1M count=20
	zpool sync

	#
	# Read the file with slow io injected on the disks
	# This will cause multiple errors on each disk to trip ZED SERD
	#
	#   pool: slow_io_pool
	#  state: ONLINE
	# config:
	#
	#         NAME                           STATE  READ WRITE CKSUM  SLOW
	#         slow_io_pool                   ONLINE    0     0     0     -
	#           raidz1-0                     ONLINE    0     0     0     -
	#             /var/tmp/vdevfile1.499278  ONLINE    0     0     0   113
	#             /var/tmp/vdevfile2.499278  ONLINE    0     0     0   109
	#             /var/tmp/vdevfile3.499278  ONLINE    0     0     0    96
	#             /var/tmp/vdevfile4.499278  ONLINE    0     0     0   109
	#
	start_slow_io
	dd if=$FILEPATH of=/dev/null bs=1M count=20 2>/dev/null
	stop_slow_io

	# count events available for processing
	typeset -i i=0
	typeset -i events=0
	while [[ $i -lt 60 ]]; do
		events=$(zpool events | grep "ereport\.fs\.zfs.delay" | wc -l)
		[[ $events -ge "50" ]] && break
		i=$((i+1))
		sleep 1
	done
	log_note "$events delay events found"
	if [[ $events -lt "50" ]]; then
		log_note "bailing: not enough events to complete the test"
		destroy_pool $TESTPOOL
		return
	fi

	#
	# give slow ZED a chance to process the delay events
	#
	typeset -i i=0
	typeset -i skips=0
	while [[ $i -lt 75 ]]; do
		skips=$(grep "retiring case" \
			$ZEDLET_DIR/zed.log | wc -l)
		[[ $skips -gt "0" ]] && break
		i=$((i+1))
		sleep 1
	done

	log_note $skips degrade skips in ZED log after $i seconds
	[ $skips -gt "0" ] || log_fail "expecting to see skips"

	degrades=$(grep "zpool_vdev_degrade" $ZEDLET_DIR/zed.log | wc -l)
	log_note $degrades vdev degrades in ZED log
	[ $degrades -eq "0" ] || \
		log_fail "expecting no degrade events, found $degrades"

	destroy_pool $TESTPOOL
}

log_assert "Test ZED slow io across multiple vdevs"
log_onexit cleanup

log_must zed_events_drain
log_must zed_start
multiple_slow_vdevs_test

log_pass "Test ZED slow io across multiple vdevs"
