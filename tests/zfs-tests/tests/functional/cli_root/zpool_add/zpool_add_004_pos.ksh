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
log_must zpool add $TESTPOOL $ZVOL_DEVDIR/$TESTPOOL1/$TESTVOL

log_must vdevs_in_pool "$TESTPOOL" "$ZVOL_DEVDIR/$TESTPOOL1/$TESTVOL"

# Give zed a chance to finish processing the event, otherwise
# a race condition can lead to stuck "zpool destroy $TESTPOOL"
sleep 1

log_pass "'zpool add <pool> <vdev> ...' adds zfs volume to the pool successfully"
