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

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
log_must mkfile 1g $TMPDIR/dsk1
log_must mkfile 1g $TMPDIR/dsk2
DISKS="$TMPDIR/dsk1 $TMPDIR/dsk2"
REMOVEDISK=$TMPDIR/dsk1

default_setup_noexit "$DISKS"

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISKS
}

log_onexit cleanup

log_must zfs set compression=off $TESTPOOL/$TESTFS

# Write a little under half the pool.
log_must file_write -o create -f $TESTDIR/$TESTFILE1 -b $((2**20)) -c $((2**9))

#
# Start a writing thread to ensure the removal will take a while.
# This will automatically die when we destroy the pool.
#
start_random_writer $TESTDIR/$TESTFILE1

function callback
{
	# Attempt to write more than the new pool will be able to handle.
	file_write -o create -f $TESTDIR/$TESTFILE2 -b $((2**20)) -c $((2**9))
	zret=$?
	ENOSPC=28
	log_note "file_write returned $zret"
	(( $zret == $ENOSPC )) || log_fail "Did not get ENOSPC during removal."
}

log_must attempt_during_removal $TESTPOOL $REMOVEDISK callback

log_pass "Removal properly sets reservation."
