#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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

log_must $ZPOOL create -o cachefile=none $TESTPOOL $DISKS
typeset DEVICEDIR=$(get_device_dir $DISKS)
log_mustnot pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -o cachefile=none -d $DEVICEDIR $TESTPOOL
log_mustnot pool_in_cache $TESTPOOL

log_must $ZPOOL export $TESTPOOL
log_must $ZPOOL import -o cachefile=$CPATH -d $DEVICEDIR $TESTPOOL
log_must pool_in_cache $TESTPOOL

log_must $ZPOOL destroy $TESTPOOL

log_pass "Importing a pool with \"cachefile\" set doesn't update zpool.cache"
