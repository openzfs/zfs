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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cachefile/cachefile.cfg
. $STF_SUITE/tests/functional/cachefile/cachefile.kshlib

#
# DESCRIPTION:
#
# Importing a pool with "cachefile" set doesn't update zpool.cache
#
# STRATEGY:
# 1. Create a pool with the cachefile property set
# 2. Verify the pool doesn't have an entry in zpool.cache
# 3. Export the pool
# 4. Import the pool
# 5. Verify the pool does have an entry in zpool.cache
# 6. Export the pool
# 7. Import the pool -o cachefile=<cachefile>
# 8. Verify the pool doesn't have an entry in zpool.cache
#

function cleanup
{
	if poolexists $TESTPOOL ; then
                destroy_pool $TESTPOOL
        fi
}

verify_runnable "global"

log_assert "Importing a pool with \"cachefile\" set doesn't update zpool.cache"
log_onexit cleanup

log_must zpool create -o cachefile=none $TESTPOOL $DISKS
typeset DEVICEDIR=$(get_device_dir $DISKS)
log_mustnot pool_in_cache $TESTPOOL

log_must zpool export $TESTPOOL
log_must zpool import -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_must zpool export $TESTPOOL
log_must zpool import -o cachefile=none -d $DEVICEDIR $TESTPOOL
log_mustnot pool_in_cache $TESTPOOL

log_must zpool export $TESTPOOL
log_must zpool import -o cachefile=$CPATH -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_must zpool destroy $TESTPOOL

log_pass "Importing a pool with \"cachefile\" set doesn't update zpool.cache"
