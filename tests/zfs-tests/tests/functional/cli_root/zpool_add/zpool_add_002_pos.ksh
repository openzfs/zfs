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
#	'zpool add -f <pool> <vdev> ...' can successfully add the specified
# devices to given pool in some cases.
#
# STRATEGY:
#	1. Create a mirrored pool
#	2. Without -f option to add 1-way device the mirrored pool will fail
#	3. Use -f to override the errors to add 1-way device to the mirrored
#	pool
#	4. Verify the device is added successfully
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool add -f <pool> <vdev> ...' can successfully add" \
	"devices to the pool in some cases."

log_onexit cleanup

create_pool $TESTPOOL mirror $DISK0 $DISK1
log_must poolexists $TESTPOOL

log_mustnot zpool add $TESTPOOL $DISK2
log_mustnot vdevs_in_pool $TESTPOOL $DISK2

log_must zpool add -f $TESTPOOL $DISK2
log_must vdevs_in_pool $TESTPOOL $DISK2

log_must zpool destroy $TESTPOOL

create_pool $TESTPOOL mirror $DISK0 $DISK1
log_must poolexists $TESTPOOL

log_mustnot zpool add $TESTPOOL $DISK2
log_mustnot vdevs_in_pool $TESTPOOL $DISK2

log_must zpool add --allow-replication-mismatch $TESTPOOL $DISK2
log_must vdevs_in_pool $TESTPOOL $DISK2

log_pass "'zpool add -f <pool> <vdev> ...' executes successfully."
