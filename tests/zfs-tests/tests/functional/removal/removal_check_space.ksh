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

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
log_must mkfile $MINVDEVSIZE $TMPDIR/dsk1
log_must mkfile $MINVDEVSIZE $TMPDIR/dsk2
DISKS="$TMPDIR/dsk1 $TMPDIR/dsk2"
REMOVEDISK=$TMPDIR/dsk1

log_must default_setup_noexit "$DISKS"

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISKS
}
log_onexit cleanup

# Write a little more than half the pool.
log_must dd if=/dev/urandom of=/$TESTDIR/$TESTFILE0 bs=$((2**20)) \
    count=$((MINVDEVSIZE / (1024 * 1024)))
log_mustnot zpool remove $TESTPOOL $TMPDIR/dsk1

log_pass "Removal will not succeed if insufficient space."
