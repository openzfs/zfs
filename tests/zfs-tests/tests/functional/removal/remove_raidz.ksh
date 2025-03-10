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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

DISKDIR=$(mktemp -d)
log_must mkfile $MINVDEVSIZE $DISKDIR/dsk1
log_must mkfile $MINVDEVSIZE $DISKDIR/dsk2
log_must mkfile $MINVDEVSIZE $DISKDIR/dsk3
DISKS1="$DISKDIR/dsk1"
DISKS2="$DISKDIR/dsk2 $DISKDIR/dsk3"
DISKS="$DISKS1 $DISKS2"

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

log_must default_setup_noexit "$DISKS1 raidz $DISKS2"
log_onexit cleanup

# Attempt to remove the non raidz disk.
log_mustnot zpool remove $TESTPOOL $DISKDIR/dsk1

# Attempt to remove one of the raidz disks.
log_mustnot zpool remove $TESTPOOL $DISKDIR/dsk2

# Attempt to remove the raidz.
log_mustnot zpool remove $TESTPOOL raidz1-1

log_pass "Removal will not succeed if there is a top level mirror."
