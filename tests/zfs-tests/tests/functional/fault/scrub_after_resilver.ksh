#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

#
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Test the scrub after resilver zedlet
#
# STRATEGY:
# 1. Create a mirrored pool
# 2. Fault a disk
# 3. Replace the disk, starting a resilver
# 4. Verify that a scrub happens after the resilver finishes
#

log_assert "Testing the scrub after resilver zedlet"

# Backup our zed.rc
zedrc_backup=$(zed_rc_backup)

# Enable ZED_SCRUB_AFTER_RESILVER in zed.rc
zed_rc_set ZED_SCRUB_AFTER_RESILVER 1

function cleanup
{
	# Restore our zed.rc
	log_must zed_rc_restore $zedrc_backup
	default_cleanup_noexit
	log_must zpool labelclear -f $DISK1
}

log_onexit cleanup

verify_disk_count "$DISKS" 3
default_mirror_setup_noexit $DISK1 $DISK2

log_must zpool offline -f $TESTPOOL $DISK1

# Write to our degraded pool so we have some data to resilver
log_must mkfile 16M $TESTDIR/file1

# Replace the failed disks, forcing a resilver
log_must zpool replace $TESTPOOL $DISK1 $DISK3

# Wait for the resilver to finish, and then the subsequent scrub to finish.
# Waiting for the scrub has the effect of waiting for both.  Timeout after 10
# seconds if nothing is happening.
log_must wait_scrubbed $TESTPOOL
log_pass "Successfully ran the scrub after resilver zedlet"
