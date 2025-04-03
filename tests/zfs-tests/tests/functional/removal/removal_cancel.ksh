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
# DESCRIPTION:
#
# Ensure that cancelling a removal midway does not cause any
# issues like cause a panic.
#
# STRATEGY:
#
# 1. Create a pool with one vdev and do some writes on it.
# 2. Add a new vdev to the pool and start the removal of
#    the first vdev.
# 3. Cancel the removal after some segments have been copied
#    over to the new vdev.
# 4. Run zdb to ensure the on-disk state of the pool is ok.
#

function cleanup
{
	#
	# Reset tunable.
	#
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
}
log_onexit cleanup

SAMPLEFILE=/$TESTDIR/00

#
# Create pool with one disk.
#
log_must default_setup_noexit "$REMOVEDISK"

#
# Create a file of size 1GB and then do some random writes.
# Since randwritecomp does 8K writes we do 25000 writes
# which means we write ~200MB to the vdev.
#
log_must mkfile -n 1g $SAMPLEFILE
log_must randwritecomp $SAMPLEFILE 25000

#
# Add second device where all the data will be evacuated.
#
log_must zpool add -f $TESTPOOL $NOTREMOVEDISK

#
# Block removal.
#
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1

#
# Start removal.
#
log_must zpool remove $TESTPOOL $REMOVEDISK

#
# Only for debugging purposes in test logs.
#
log_must zpool status $TESTPOOL

#
# Cancel removal.
#
log_must zpool remove -s $TESTPOOL

#
# Verify on-disk state.
#
log_must zdb $TESTPOOL

log_pass "Device removal thread cancelled successfully."
