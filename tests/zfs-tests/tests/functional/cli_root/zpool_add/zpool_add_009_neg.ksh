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
#       'zpool add' should return fail if vdevs are the same or vdev is
# contained in the given pool
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add the two same devices to pool A
#	3. Add the device in pool A to pool A again
#

verify_runnable "global"

function cleanup
{
	destroy_pool -f $TESTPOOL

	# Don't want to repartition the disk(s) on Linux.
	# We do that in setup.ksh in a very special way.
	[[ -z "$LINUX" ]] && partition_cleanup
}

log_assert "'zpool add' should fail if vdevs are the same or vdev is " \
	"contained in the given pool."

log_onexit cleanup

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

create_pool "$TESTPOOL" "${disk}${slice_part}${SLICE0}"
log_must poolexists "$TESTPOOL"

log_mustnot $ZPOOL add -f "$TESTPOOL" ${disk}${slice_part}${SLICE1} ${disk}${slice_part}${SLICE1}
log_mustnot $ZPOOL add -f "$TESTPOOL" ${disk}${slice_part}${SLICE0}

log_pass "'zpool add' get fail as expected if vdevs are the same or vdev is " \
	"contained in the given pool."
