#!/bin/ksh -p
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify the number of IO and checksum events match the error counters
#	in zpool status.
#
# STRATEGY:
#	1. Create a mirror, raidz, or draid pool
#	2. Inject read/write IO errors or checksum errors
#	3. Verify the number of errors in zpool status match the corresponding
#	   number of error events.
#	4. Repeat for all combinations of mirror/raidz/draid and io/checksum
#	   errors.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

MOUNTDIR=$TEST_BASE_DIR/mount
VDEV1=$TEST_BASE_DIR/file1
VDEV2=$TEST_BASE_DIR/file2
VDEV3=$TEST_BASE_DIR/file3
POOL=error_pool
FILESIZE=$((20 * 1024 * 1024))
OLD_CHECKSUMS=$(get_tunable CHECKSUM_EVENTS_PER_SECOND)
OLD_LEN_MAX=$(get_tunable ZEVENT_LEN_MAX)

function cleanup
{
	log_must set_tunable64 CHECKSUM_EVENTS_PER_SECOND $OLD_CHECKSUMS
	log_must set_tunable64 ZEVENT_LEN_MAX $OLD_LEN_MAX

	log_must zinject -c all
	log_must zpool events -c
	if poolexists $POOL ; then
		log_must destroy_pool $POOL
	fi
	log_must rm -fd $VDEV1 $VDEV2 $VDEV3 $MOUNTDIR
}

log_assert "Check that the number of zpool errors match the number of events"

log_onexit cleanup

# Set our thresholds high so we never ratelimit or drop events.
set_tunable64 CHECKSUM_EVENTS_PER_SECOND 20000
set_tunable64 ZEVENT_LEN_MAX 20000

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3
log_must mkdir -p $MOUNTDIR

# Run error test on a specific type of pool
#
# $1: pool - mirror, raidz, draid
# $2: test type - corrupt (checksum error), io
# $3: read, write
function do_test
{
	POOLTYPE=$1
	ERR=$2
	RW=$3

	log_note "Testing $ERR $RW on $POOLTYPE"
	log_must zpool create -f -m $MOUNTDIR -o failmode=continue $POOL $POOLTYPE $VDEV1 $VDEV2 $VDEV3
	log_must zpool events -c
	log_must zfs set compression=off $POOL

	if [ "$RW" == "read" ] ; then
		log_must mkfile $FILESIZE $MOUNTDIR/file
	fi

	log_must zinject -d $VDEV1 -e $ERR -T $RW -f 100 $POOL

	if [ "$RW" == "write" ] ; then
		log_must mkfile $FILESIZE $MOUNTDIR/file
		sync_pool $POOL
	else
		log_must zpool scrub $POOL
		wait_scrubbed $POOL
	fi

	log_must zinject -c all

	# Wait for the pool to settle down and finish resilvering (if
	# necessary).  We want the errors to stop incrementing before we
	# check the error and event counts.
	while is_pool_resilvering $POOL ; do
		sleep 1
	done

	out="$(zpool status -p | grep $VDEV1)"

	if [ "$ERR" == "corrupt" ] ; then
		events=$(zpool events | grep -c checksum)
		val=$(echo "$out" | awk '{print $5}')
		str="checksum"
	elif [ "$ERR" == "io" ] ; then
		allevents=$(zpool events | grep io)
		events=$(echo "$allevents" | wc -l)
		if [ "$RW" == "read" ] ; then
			str="read IO"
			val=$(echo "$out" | awk '{print $3}')
		else
			str="write IO"
			val=$(echo "$out" | awk '{print $4}')
		fi
	fi

	if [ -z "$val" -o $val -eq 0 -o -z "$events" -o $events -eq 0 ] ; then
		log_fail "Didn't see any errors or events ($val/$events)"
	fi

	if [ $val -ne $events ] ; then
		log_fail "$val $POOLTYPE $str errors != $events events"
	else
		log_note "$val $POOLTYPE $str errors == $events events"
	fi

	log_must zpool destroy $POOL
}

# Test all types of errors on mirror, raidz, and draid pools
for pooltype in mirror raidz draid; do
	do_test $pooltype corrupt read
	do_test $pooltype io read
	do_test $pooltype io write
done

log_pass "The number of errors matched the number of events"
