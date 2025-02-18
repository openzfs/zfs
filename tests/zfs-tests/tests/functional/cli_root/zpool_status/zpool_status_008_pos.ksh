#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zpool status -e' only shows unhealthy devices.
#
# STRATEGY:
# 1. Create zpool
# 2. Force DEGRADE, FAULT, or inject slow IOs for vdevs
# 3. Verify vdevs are reported correctly with -e and -s
# 4. Verify parents are reported as DEGRADED
# 5. Verify healthy children are not reported
#

function cleanup
{
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	zinject -c all
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -f $all_vdevs
}

log_assert "Verify 'zpool status -e'"

log_onexit cleanup

all_vdevs=$(echo $TESTDIR/vdev{1..6})
log_must mkdir -p $TESTDIR
log_must truncate -s $MINVDEVSIZE $all_vdevs

OLD_SLOW_IO=$(get_tunable ZIO_SLOW_IO_MS)

for raid_type in "draid2:3d:6c:1s" "raidz2"; do

	log_must zpool create -f $TESTPOOL2 $raid_type $all_vdevs

	# Check DEGRADED vdevs are shown.
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev4 "ONLINE"
	log_must zinject -d $TESTDIR/vdev4 -A degrade $TESTPOOL2
	log_must eval "zpool status -e $TESTPOOL2 | grep $TESTDIR/vdev4 | grep DEGRADED"

	# Check FAULTED vdevs are shown.
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev5 "ONLINE"
	log_must zinject -d $TESTDIR/vdev5 -A fault $TESTPOOL2
	log_must eval "zpool status -e $TESTPOOL2 | grep $TESTDIR/vdev5 | grep FAULTED"

	# Check no ONLINE vdevs are shown
	log_mustnot eval "zpool status -e $TESTPOOL2 | grep ONLINE"

	# Check no ONLINE slow vdevs are show.  Then mark IOs greater than
	# 750ms slow, delay IOs 1000ms to vdev6, check slow IOs.
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev6 "ONLINE"
	log_mustnot eval "zpool status -es $TESTPOOL2 | grep ONLINE"

	log_must set_tunable64 ZIO_SLOW_IO_MS 750
	log_must zinject -d $TESTDIR/vdev6 -D1000:100 $TESTPOOL2
	log_must mkfile 1048576 /$TESTPOOL2/testfile
	sync_pool $TESTPOOL2
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must zinject -c all

	# Check vdev6 slow IOs are only shown when requested with -s.
	log_mustnot eval "zpool status -e $TESTPOOL2 | grep $TESTDIR/vdev6 | grep ONLINE"
	log_must eval "zpool status -es $TESTPOOL2 | grep $TESTDIR/vdev6 | grep ONLINE"

	# Pool level and top-vdev level status must be DEGRADED.
	log_must eval "zpool status -e $TESTPOOL2 | grep $TESTPOOL2 | grep DEGRADED"
	log_must eval "zpool status -e $TESTPOOL2 | grep $raid_type | grep DEGRADED"

	# Check that healthy vdevs[1-3] aren't shown with -e.
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev1 "ONLINE"
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev2 "ONLINE"
	log_must check_vdev_state $TESTPOOL2 $TESTDIR/vdev3 "ONLINE"
	log_mustnot eval "zpool status -es $TESTPOOL2 | grep $TESTDIR/vdev1 | grep ONLINE"
	log_mustnot eval "zpool status -es $TESTPOOL2 | grep $TESTDIR/vdev2 | grep ONLINE"
	log_mustnot eval "zpool status -es $TESTPOOL2 | grep $TESTDIR/vdev3 | grep ONLINE"

	log_must zpool status -es $TESTPOOL2

	log_must zpool destroy $TESTPOOL2
done

log_pass "Verify zpool status -e shows only unhealthy vdevs"
