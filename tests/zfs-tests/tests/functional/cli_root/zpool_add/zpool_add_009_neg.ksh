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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#       'zpool add' should return fail if vdevs are the same or vdev is
# contained in the given pool
#
# STRATEGY:
#	1. Create a storage pool
#	2. Add the device in pool A to pool A again
#	3. Add the two devices to pool A in the loop, one of them already
#	added or same device added multiple times
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool add' should fail if vdevs are the same or vdev is " \
	"contained in the given pool."

log_onexit cleanup

create_pool $TESTPOOL $DISK0
log_must poolexists $TESTPOOL

log_mustnot zpool add -f $TESTPOOL $DISK0

for type in "" "mirror" "raidz" "draid" "spare" "log" "dedup" "special" "cache"
do
	log_mustnot zpool add -f $TESTPOOL $type $DISK0 $DISK1
	log_mustnot zpool add --allow-in-use $TESTPOOL $type $DISK0 $DISK1
	log_mustnot zpool add -f $TESTPOOL $type $DISK1 $DISK1
	log_mustnot zpool add --allow-in-use $TESTPOOL $type $DISK1 $DISK1
done

log_pass "'zpool add' get fail as expected if vdevs are the same or vdev is " \
	"contained in the given pool."
