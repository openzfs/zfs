#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
#	'zpool add -n <pool> <vdev> ...' can display the configuration without
# adding the specified devices to given pool
#
# STRATEGY:
#	1. Create a storage pool
#	2. Use -n to add a device to the pool
#	3. Verify the device is not added actually
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && \
                destroy_pool $TESTPOOL

	partition_cleanup

	[[ -e $tmpfile ]] && \
		log_must rm -f $tmpfile
}

log_assert "'zpool add -n <pool> <vdev> ...' can display the configuration" \
	"without actually adding devices to the pool."

log_onexit cleanup

tmpfile="$TEST_BASE_DIR/zpool_add_003.tmp$$"

create_pool "$TESTPOOL" "${disk}${SLICE_PREFIX}${SLICE0}"
log_must poolexists "$TESTPOOL"

zpool add -n "$TESTPOOL" ${disk}${SLICE_PREFIX}${SLICE1} > $tmpfile

log_mustnot vdevs_in_pool "$TESTPOOL" "${disk}${SLICE_PREFIX}${SLICE1}"

str="would update '$TESTPOOL' to the following configuration:"
cat $tmpfile | grep "$str" >/dev/null 2>&1
(( $? != 0 )) && \
	 log_fail "'zpool add -n <pool> <vdev> ...' is executed as unexpected"

log_pass "'zpool add -n <pool> <vdev> ...'executes successfully."
