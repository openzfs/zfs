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
# Copyright (c) 2024, Klara Inc.
#

# DESCRIPTION:
#	Verify that simultaneous io error events from multiple vdevs
#	doesn't generate a fault
#
# STRATEGY:
#	1. Create a pool with a 4 disk raidz vdev
#	2. Inject io errors
#	3. Verify that ZED detects the errors but doesn't fault any vdevs
#

. $STF_SUITE/include/libtest.shlib

TESTDIR="$TEST_BASE_DIR/zed_error_multiple"
VDEV1="$TEST_BASE_DIR/vdevfile1.$$"
VDEV2="$TEST_BASE_DIR/vdevfile2.$$"
VDEV3="$TEST_BASE_DIR/vdevfile3.$$"
VDEV4="$TEST_BASE_DIR/vdevfile4.$$"
VDEVS="$VDEV1 $VDEV2 $VDEV3 $VDEV4"
TESTPOOL="zed_test_pool"
FILEPATH="$TESTDIR/zed.testfile"

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
}

function start_io_errors
{
	for vdev in $VDEVS
	do
		log_must zpool set io_n=4 $TESTPOOL $vdev
		log_must zpool set io_t=60 $TESTPOOL $vdev
	done
	zpool sync

	for vdev in $VDEVS
	do
		log_must zinject -d $vdev -e io $TESTPOOL
	done
	zpool sync
}

function multiple_slow_vdevs_test
{
	log_must truncate -s 1G $VDEVS
	default_raidz_setup_noexit $VDEVS

	log_must zpool events -c
	log_must zfs set compression=off $TESTPOOL
	log_must zfs set primarycache=none $TESTPOOL
	log_must zfs set recordsize=4K $TESTPOOL

	log_must dd if=/dev/urandom of=$FILEPATH bs=1M count=4
	zpool sync

	#
	# Read the file with io errors injected on the disks
	# This will cause multiple errors on each disk to trip ZED SERD
	#
	#   pool: zed_test_pool
	#  state: ONLINE
	# status: One or more devices has experienced an unrecoverable error.  An
	#         attempt was made to correct the error.  Applications are unaffected.
	# action: Determine if the device needs to be replaced, and clear the errors
	#         using 'zpool clear' or replace the device with 'zpool replace'.
	#    see: https://openzfs.github.io/openzfs-docs/msg/ZFS-8000-9P
	# config:
	#
	#         NAME                            STATE     READ WRITE CKSUM
	#         zed_test_pool                   ONLINE       0     0     0
	#           raidz1-0                      ONLINE       0     0     0
	#             /var/tmp/vdevfile1.1547063  ONLINE     532   561     0
	#             /var/tmp/vdevfile2.1547063  ONLINE     547   594     0
	#             /var/tmp/vdevfile3.1547063  ONLINE   1.05K 1.10K     0
	#             /var/tmp/vdevfile4.1547063  ONLINE   1.05K 1.00K     0
	#

	start_io_errors
	dd if=$FILEPATH of=/dev/null bs=1M count=4 2>/dev/null
	log_must zinject -c all

	# count io error events available for processing
	typeset -i i=0
	typeset -i events=0
	while [[ $i -lt 60 ]]; do
		events=$(zpool events | grep "ereport\.fs\.zfs.io" | wc -l)
		[[ $events -ge "50" ]] && break
		i=$((i+1))
		sleep 1
	done
	log_note "$events io error events found"
	if [[ $events -lt "50" ]]; then
		log_note "bailing: not enough events to complete the test"
		destroy_pool $TESTPOOL
		return
	fi

	#
	# give slow ZED a chance to process the checkum events
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

	log_note $skips fault skips in ZED log after $i seconds
	[ $skips -gt "0" ] || log_fail "expecting to see skips"

	fault=$(grep "zpool_vdev_fault" $ZEDLET_DIR/zed.log | wc -l)
	log_note $fault vdev fault in ZED log
	[ $fault -eq "0" ] || \
		log_fail "expecting no fault events, found $fault"

	destroy_pool $TESTPOOL
}

log_assert "Test ZED io errors across multiple vdevs"
log_onexit cleanup

log_must zed_events_drain
log_must zed_start
multiple_slow_vdevs_test

log_pass "Test ZED io errors across multiple vdevs"
