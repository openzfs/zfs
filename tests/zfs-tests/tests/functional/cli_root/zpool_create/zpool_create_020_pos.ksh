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
