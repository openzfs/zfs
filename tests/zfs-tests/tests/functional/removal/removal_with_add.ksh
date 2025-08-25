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
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

DISKDIR=$(mktemp -d)
log_must mkfile 1g $DISKDIR/dsk1
log_must mkfile 1g $DISKDIR/dsk2

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

default_setup_noexit "$DISKS"
log_onexit cleanup

function callback
{
	log_mustnot zpool attach -f $TESTPOOL $DISKDIR/dsk1 $DISKDIR/dsk2
	log_mustnot zpool add -f $TESTPOOL \
	    raidz $DISKDIR/dsk1 $DISKDIR/dsk2
	log_must zpool add -f $TESTPOOL $DISKDIR/dsk1
	return 0
}

test_removal_with_operation callback

log_pass "Removal can only add normal disks."
