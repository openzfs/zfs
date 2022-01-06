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
# Copyright (c) 2021 by Delphix. All rights reserved.
#

# DESCRIPTION:
#	Verify that new errors after a pool scrub are considered a duplicate
#
# STRATEGY:
#	1. Create a raidz pool with a file
#	2. Inject garbage into one of the vdevs
#	3. Scrub the pool
#	4. Observe the checksum error counts
#	5. Repeat inject and pool scrub
#	6. Verify that second pass also produces similar errors (i.e. not
#	   treated as a duplicate)
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

MOUNTDIR=$TEST_BASE_DIR/mount
FILEPATH=$MOUNTDIR/target
VDEV1=$TEST_BASE_DIR/vfile1
VDEV2=$TEST_BASE_DIR/vfile2
VDEV3=$TEST_BASE_DIR/vfile3
SUPPLY=$TEST_BASE_DIR/supply
POOL=test_pool
FILESIZE="15M"
DAMAGEBLKS=10

OLD_LEN_MAX=$(get_tunable ZEVENT_LEN_MAX)
RETAIN_MAX=$(get_tunable ZEVENT_RETAIN_MAX)
OLD_CHECKSUMS=$(get_tunable CHECKSUM_EVENTS_PER_SECOND)

EREPORTS="$STF_SUITE/tests/functional/cli_root/zpool_events/ereports"

function cleanup
{
	log_must set_tunable64 CHECKSUM_EVENTS_PER_SECOND $OLD_CHECKSUMS
	log_must set_tunable64 ZEVENT_LEN_MAX $OLD_LEN_MAX

	zpool events -c
	if poolexists $POOL ; then
		zpool export $POOL
	fi
	log_must rm -f $VDEV1 $VDEV2 $VDEV3
}

function damage_and_repair
{
	log_must zpool clear $POOL $VDEV1
	log_must zpool events -c

	log_note injecting damage to $VDEV1
	log_must dd conv=notrunc if=$SUPPLY of=$VDEV1 bs=1M seek=4 count=$DAMAGEBLKS
	log_must zpool scrub $POOL
	log_must zpool wait -t scrub $POOL
	log_note "pass $1 observed $($EREPORTS | grep -c checksum) checksum ereports"

	repaired=$(zpool status $POOL | grep "scan: scrub repaired" | awk '{print $4}')
	if [ "$repaired" == "0B" ]; then
		log_fail "INVALID TEST -- expected scrub to repair some blocks"
	else
		log_note "$repaired repaired during scrub"
	fi
}

function checksum_error_count
{
	zpool status -p $POOL | grep $VDEV1 | awk '{print $5}'
}

assertion="Damage to recently repaired blocks should be reported/counted"
log_assert "$assertion"
log_note "zevent retain max setting: $RETAIN_MAX"

log_onexit cleanup

# Set our threshold high to avoid dropping events.
set_tunable64 ZEVENT_LEN_MAX 20000
set_tunable64 CHECKSUM_EVENTS_PER_SECOND 20000

# Initialize resources for the test
log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3
log_must dd if=/dev/urandom of=$SUPPLY bs=1M count=$DAMAGEBLKS
log_must mkdir -p $MOUNTDIR
log_must zpool create -f -m $MOUNTDIR -o failmode=continue $POOL raidz $VDEV1 $VDEV2 $VDEV3
log_must zfs set compression=off recordsize=16k $POOL
# create a file full of zeros
log_must mkfile -v $FILESIZE $FILEPATH
sync_pool $POOL

# run once and observe the checksum errors
damage_and_repair 1
errcnt=$(checksum_error_count)
log_note "$errcnt errors observed"
# set expectaton of at least 75% of what we observed in first pass
(( expected = (errcnt * 75) / 100 ))

# run again and we should observe new checksum errors
damage_and_repair 2
errcnt=$(checksum_error_count)

log_must zpool destroy $POOL

if (( errcnt < expected )); then
	log_fail "FAILED -- expecting at least $expected checksum errors but only observed $errcnt"
else
	log_note observed $errcnt new checksum errors after a scrub
	log_pass "$assertion"
fi

