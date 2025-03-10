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

DISKDIR=$(mktemp -d)
DISK1="$DISKDIR/dsk1"
DISK2="$DISKDIR/dsk2"
DISK3="$DISKDIR/dsk3"
DISKS="$DISK1 $DISK2 $DISK3"

log_must mkfile $(($MINVDEVSIZE * 2)) $DISK1
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK2
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK3

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

log_must default_setup_noexit "$DISK1 mirror $DISK2 $DISK3"
log_onexit cleanup

# Attempt to remove the non mirrored disk.
log_must zpool remove $TESTPOOL $DISK1

# Attempt to remove one of the disks in the mirror.
log_mustnot zpool remove $TESTPOOL $DISK2
log_must wait_for_removal $TESTPOOL

# Add back the first disk.
# We use -f as we're adding a single vdev to zpool with only mirrors.
log_must zpool add -f $TESTPOOL $DISK1

# Now attempt to remove the mirror.
log_must zpool remove $TESTPOOL mirror-1

log_pass "Removal will succeed for top level vdev(s)."
