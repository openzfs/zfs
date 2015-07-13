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
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib
. $TMPFILE

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
	destroy_pool -f $TESTPOOL

	# Don't want to repartition the disk(s) on Linux.
	# We do that in setup.ksh in a very special way.
	[[ -z "$LINUX" ]] && partition_cleanup
}

log_assert "'zpool add -f <pool> <vdev> ...' can successfully add" \
	"devices to the pool in some cases."

log_onexit cleanup

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

create_pool "$TESTPOOL" mirror "${disk}${slice_part}${SLICE0}" "${disk}${slice_part}${SLICE1}"
log_must poolexists "$TESTPOOL"

log_mustnot $ZPOOL add "$TESTPOOL" ${disk}${slice_part}${SLICE3}
log_mustnot iscontained "$TESTPOOL" "${disk}${slice_part}${SLICE3}"

log_must $ZPOOL add -f "$TESTPOOL" ${disk}${slice_part}${SLICE3}
log_must iscontained "$TESTPOOL" "${disk}${slice_part}${SLICE3}"

log_pass "'zpool add -f <pool> <vdev> ...' executes successfully."
