#! /bin/ksh -p
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
# Copyright (c) 2014, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}

DISK1="$TMPDIR/dsk1"
DISK2="$TMPDIR/dsk2"
DISK3="$TMPDIR/dsk3"
DISK4="$TMPDIR/dsk4"
DISKS="$DISK1 $DISK2 $DISK3 $DISK4"

log_must mkfile $(($MINVDEVSIZE * 2)) $DISK1
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK2
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK3
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK4

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISKS
}

# Build a zpool with 2 mirror vdevs
log_must default_setup_noexit "mirror $DISK1 $DISK2 mirror $DISK3 $DISK4"
log_onexit cleanup

# Remove one of the mirrors
log_must zpool remove $TESTPOOL mirror-1
log_must wait_for_removal $TESTPOOL

# Attempt to add a single-device vdev, shouldn't work
log_mustnot zpool add $TESTPOOL $DISK3

# Force it, should work
log_must zpool add -f $TESTPOOL $DISK3

log_pass "Prevented from adding a non-mirror vdev on a mirrored zpool w/ indirect vdevs"
