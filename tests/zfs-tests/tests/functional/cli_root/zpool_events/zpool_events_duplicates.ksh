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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

# DESCRIPTION:
#	Verify that duplicate I/O ereport errors are not posted
#
# STRATEGY:
#	1. Create a mirror pool
#	2. Inject duplicate read/write IO errors and checksum errors
#	3. Verify there are no duplicate events being posted
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

MOUNTDIR=$TEST_BASE_DIR/mount
FILEPATH=$MOUNTDIR/badfile
VDEV1=$TEST_BASE_DIR/vfile1
VDEV2=$TEST_BASE_DIR/vfile2
POOL=error_pool
FILESIZE="10M"
OLD_LEN_MAX=$(get_tunable ZEVENT_LEN_MAX)
RETAIN_MAX=$(get_tunable ZEVENT_RETAIN_MAX)

duplicates=false

function cleanup
{
	log_must set_tunable64 ZEVENT_LEN_MAX $OLD_LEN_MAX

	log_must zinject -c all
	if poolexists $POOL ; then
		destroy_pool $POOL
	fi
	log_must rm -fd $VDEV1 $VDEV2 $MOUNTDIR
}

log_assert "Duplicate I/O ereport errors are not posted"
log_note "zevent retain max setting: $RETAIN_MAX"

log_onexit cleanup

# Set our threshold high to avoid dropping events.
set_tunable64 ZEVENT_LEN_MAX 20000

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must mkdir -p $MOUNTDIR

#
# $1: test type - corrupt (checksum error), io
# $2: read, write
function do_dup_test
{
	ERR=$1
	RW=$2

	log_note "Testing $ERR $RW ereports"
	log_must zpool create -f -m $MOUNTDIR -o failmode=continue $POOL mirror $VDEV1 $VDEV2
	log_must zpool events -c
	log_must zfs set compression=off $POOL

	if [ "$RW" == "read" ] ; then
		log_must mkfile $FILESIZE $FILEPATH

		# unmount and mount filesystems to purge file from ARC
		# to force reads to go through error inject handler
		log_must zfs unmount $POOL
		log_must zfs mount $POOL

		# all reads from this file get an error
		if [ "$ERR" == "corrupt" ] ; then
			log_must zinject -a -t data -e checksum -T read $FILEPATH
		else
			log_must zinject -a -t data -e io -T read $FILEPATH
		fi

		# Read the file a few times to generate some
		# duplicate errors of the same blocks
		for _ in {1..15}; do
			dd if=$FILEPATH of=/dev/null bs=128K 2>/dev/null
		done
		log_must zinject -c all
	fi

	log_must zinject -d $VDEV1 -e $ERR -T $RW -f 100 $POOL

	if [ "$RW" == "write" ] ; then
		log_must mkfile $FILESIZE $FILEPATH
		sync_pool $POOL
	fi

	log_must zinject -c all

	ereports="$(ereports | sort)"
	actual=$(echo "$ereports" | wc -l)
	unique=$(echo "$ereports" | uniq | wc -l)
	log_note "$actual total $ERR $RW ereports where $unique were unique"

	if [ $actual -gt $unique ] ; then
		log_note "UNEXPECTED -- $((actual-unique)) duplicate $ERR $RW ereports"
		echo "$ereports"
		duplicates=true
	fi

	log_must zpool destroy $POOL
}

do_dup_test "corrupt" "read"
do_dup_test "io" "read"
do_dup_test "io" "write"

if $duplicates; then
	log_fail "FAILED -- Duplicate I/O ereport errors encountered"
else
	log_pass "Duplicate I/O ereport errors are not posted"
fi
