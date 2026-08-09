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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.cfg

#
# DESCRIPTION:
#
# zpool create -R works as expected
#
# STRATEGY:
# 1. Create a -R altroot pool
# 2. Verify the pool is mounted at the correct location
# 3. Verify that cachefile=none for the pool
# 4. Verify that root=<mountpoint> for the pool
# 5. Verify that no reference to the pool is found in /etc/zfs/zpool.cache

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf /${TESTPOOL}.root
	rm -f $values
}

log_onexit cleanup

log_assert "zpool create -R works as expected"

typeset values=$TEST_BASE_DIR/values.$$

log_must rm -f /etc/zfs/zpool.cache
log_must rm -rf /${TESTPOOL}.root
log_must zpool create -R /${TESTPOOL}.root $TESTPOOL $DISK0
if [ ! -d /${TESTPOOL}.root ]
then
	log_fail "Mountpoint was not created when using zpool with -R flag!"
fi

FS=$(zfs list $TESTPOOL)
if [ -z "$FS" ]
then
	log_fail "Mounted filesystem at /${TESTPOOL}.root isn't ZFS!"
fi

log_must zpool get all $TESTPOOL
zpool get all $TESTPOOL > $values

# check for the cachefile property, verifying that it's set to 'none'
log_must grep -q "$TESTPOOL[ ]*cachefile[ ]*none" $values

# check that the root = /mountpoint property is set correctly
log_must grep -q "$TESTPOOL[ ]*altroot[ ]*/${TESTPOOL}.root" $values

rm $values

# finally, check that the pool has no reference in /etc/zfs/zpool.cache
if [[ -f /etc/zfs/zpool.cache ]] ; then
	if strings /etc/zfs/zpool.cache | grep -q ${TESTPOOL}
	then
		strings /etc/zfs/zpool.cache
		log_fail "/etc/zfs/zpool.cache appears to have a reference to $TESTPOOL"
	fi
fi

log_pass "zpool create -R works as expected"
