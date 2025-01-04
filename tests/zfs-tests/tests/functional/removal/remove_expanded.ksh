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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# BACKGROUND:
#
# ztest hit an issue where it ran zdb and zdb failed because
# it couldn't access some indirect mappings at the end of a
# vdev. The issue was that the vdev's ms_shift had changed after
# it was removed by the addition of another vdev. This test is
# a regression test for ensuring this case doesn't come up again.
#


TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
DISK0=$TMPDIR/dsk0
DISK1=$TMPDIR/dsk1
DISK2=$TMPDIR/dsk2

log_must truncate -s $MINVDEVSIZE $DISK0
log_must truncate -s $(($MINVDEVSIZE * 3)) $DISK1
log_must truncate -s $MINVDEVSIZE $DISK2

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISK0 $DISK1 $DISK2
}

#
# Setup the pool with one disk .
#
log_must default_setup_noexit "$DISK0"
log_onexit cleanup

#
# Expand vdev.
#
log_must truncate -s $(($MINVDEVSIZE * 2)) $DISK0
log_must zpool reopen $TESTPOOL
log_must zpool online -e $TESTPOOL $DISK0

#
# Fill up the whole vdev.
#
dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=8M

#
# Add another vdev and remove the first vdev creating indirect
# mappings for nearly all the allocatable space from the first
# vdev. Wait for removal to finish.
#
log_must zpool add $TESTPOOL $DISK1
log_must zpool remove $TESTPOOL $DISK0
log_must wait_for_removal $TESTPOOL

#
# Add a new vdev that will trigger a change in the config.
# Run sync once to ensure that the config actually changed.
#
log_must zpool add $TESTPOOL $DISK2
sync_all_pools

#
# Ensure that zdb does not find any problems with this.
#
log_must zdb $TESTPOOL

log_pass "Removal of expanded vdev doesn't cause any problems."
