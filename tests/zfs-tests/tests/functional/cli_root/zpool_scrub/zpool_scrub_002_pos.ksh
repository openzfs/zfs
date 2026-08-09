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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify scrub, scrub -p, and scrub -s show the right status.
#
# STRATEGY:
#	1. Create pool and create a 100MB file in it.
#	2. zpool scrub the pool and verify it's doing a scrub.
#	3. Pause scrub and verify it's paused.
#	4. Try to pause a paused scrub and make sure that fails.
#	5. Resume the paused scrub and verify scrub is again being performed.
#	6. Verify zpool scrub -s succeed when the system is scrubbing.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must rm -f $mntpnt/biggerfile
}

log_onexit cleanup

log_assert "Verify scrub, scrub -p, and scrub -s show the right status."

# Create 1G of additional data
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must file_write -b 1048576 -c 1024 -o create -d 0 -f $mntpnt/biggerfile
sync_all_pools

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_must zpool scrub -p $TESTPOOL
log_must is_pool_scrub_paused $TESTPOOL true
log_mustnot zpool scrub -p $TESTPOOL
log_must is_pool_scrub_paused $TESTPOOL true
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_scrub_stopped $TESTPOOL true

log_pass "Verified scrub, -s, and -p show expected status."
