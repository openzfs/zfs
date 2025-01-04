#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
