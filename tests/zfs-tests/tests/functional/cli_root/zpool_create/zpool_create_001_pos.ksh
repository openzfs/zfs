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
# Copyright (c) 2012, 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create <pool> <vspec> ...' can successfully create a
# new pool with a name in ZFS namespace.
#
# STRATEGY:
# 1. Create storage pools with a name in ZFS namespace with different
# vdev specs.
# 2. Verify the pool created successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $disk1 $disk2
}

log_assert "'zpool create <pool> <vspec> ...' can successfully create" \
	"a new pool with a name in ZFS namespace."

log_onexit cleanup

typeset disk1=$(create_blockfile $FILESIZE)
typeset disk2=$(create_blockfile $FILESIZE)

pooldevs="${DISK0} \
	\"${DISK0} ${DISK1}\" \
	\"${DISK0} ${DISK1} ${DISK2}\" \
	\"$disk1 $disk2\""
mirrordevs="\"${DISK0} ${DISK1}\" \
	$raidzdevs \
	\"$disk1 $disk2\""
raidzdevs="\"${DISK0} ${DISK1} ${DISK2}\""
draiddevs="\"${DISK0} ${DISK1} ${DISK2}\""

create_pool_test "$TESTPOOL" "" "$pooldevs"
create_pool_test "$TESTPOOL" "mirror" "$mirrordevs"
create_pool_test "$TESTPOOL" "raidz" "$raidzdevs"
create_pool_test "$TESTPOOL" "raidz1" "$raidzdevs"
create_pool_test "$TESTPOOL" "draid" "$draiddevs"

log_pass "'zpool create <pool> <vspec> ...' success."
