#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
