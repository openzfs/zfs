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
