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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Resilver prevent scrub from starting until the resilver completes
#
# STRATEGY:
#	1. Setup a mirror pool and filled with data.
#	2. Detach one of devices
#	3. Create a file for the resilver to work on so it takes some time
#	4. Export/import the pool to ensure the cache is dropped
#	5. Verify scrub failed until the resilver completed
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	rm -f $mntpnt/extra
}

verify_runnable "global"

log_onexit cleanup

log_assert "Resilver prevent scrub from starting until the resilver completes"

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Temporarily prevent scan progress so our test doesn't race
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

while ! is_pool_resilvering $TESTPOOL; do
	log_must zpool detach $TESTPOOL $DISK2
	log_must file_write -b 1048576 -c 128 -o create -d 0 -f $mntpnt/extra
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	log_must zpool attach $TESTPOOL $DISK1 $DISK2
done

log_must is_pool_resilvering $TESTPOOL
log_mustnot zpool scrub $TESTPOOL

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL

log_pass "Resilver prevent scrub from starting until the resilver completes"
