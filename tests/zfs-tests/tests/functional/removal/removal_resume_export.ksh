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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# When a pool has an ongoing removal and it is exported ZFS
# suspends the removal thread beforehand. This test ensures
# that ZFS restarts the removal thread if the export fails
# for some reason.
#
# STRATEGY:
#
# 1. Create a pool with one vdev and do some writes on it.
# 2. Add a new vdev to the pool and start the removal of
#    the first vdev.
# 3. Inject a fault in the pool and attempt to export (it
#    should fail).
# 4. After the export fails ensure that the removal thread
#    was restarted and the process complete successfully.
#


function cleanup
{
	zinject -c all
	default_cleanup_noexit
}

function callback
{
	#
	# Inject an error so export fails after having just suspended
	# the removal thread. [spa_inject_ref gets incremented]
	#
	log_must zinject -d $REMOVEDISK -D 10:1 $TESTPOOL

	#
	# Because of the above error export should fail.
	#
	log_mustnot zpool export $TESTPOOL

	#
	# Let the removal finish.
	#
	log_must zinject -c all

	return 0
}

log_onexit cleanup

#
# Create pool with one disk.
#
log_must default_setup_noexit "$REMOVEDISK"

#
# Turn off compression to raise capacity as much as possible
# for the little time that this test runs.
#
log_must zfs set compression=off $TESTPOOL/$TESTFS

#
# Write some data that will be evacuated from the device when
# we start the removal.
#
log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=64M count=32

#
# Add second device where all the data will be evacuated.
#
log_must zpool add -f $TESTPOOL $NOTREMOVEDISK

#
# Attempt the export with errors injected.
#
log_must attempt_during_removal $TESTPOOL $REMOVEDISK callback

log_pass "Device removal thread resumes after failed export"
