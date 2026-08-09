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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#	'zpool add <pool> <vdev> ...' can successfully add a zfs volume
# to the given pool
#
# STRATEGY:
#	1. Create a storage pool and a zfs volume
#	2. Add the volume to the pool
#	3. Verify the devices are added to the pool successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	if [ -n "$recursive" ]; then
		set_tunable64 VOL_RECURSIVE $recursive
	fi
}

log_assert "'zpool add <pool> <vdev> ...' can add zfs volume to the pool."

log_onexit cleanup

create_pool $TESTPOOL $DISK0
log_must poolexists $TESTPOOL

create_pool $TESTPOOL1 $DISK1
log_must poolexists $TESTPOOL1
log_must zfs create -V $VOLSIZE $TESTPOOL1/$TESTVOL
block_device_wait

if is_freebsd; then
	recursive=$(get_tunable VOL_RECURSIVE)
	log_must set_tunable64 VOL_RECURSIVE 1
fi
log_must zpool add --allow-ashift-mismatch $TESTPOOL $ZVOL_DEVDIR/$TESTPOOL1/$TESTVOL

log_must vdevs_in_pool "$TESTPOOL" "$ZVOL_DEVDIR/$TESTPOOL1/$TESTVOL"

# Give zed a chance to finish processing the event, otherwise
# a race condition can lead to stuck "zpool destroy $TESTPOOL"
sleep 1

log_pass "'zpool add <pool> <vdev> ...' adds zfs volume to the pool successfully"
