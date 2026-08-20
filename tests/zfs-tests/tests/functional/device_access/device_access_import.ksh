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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/device_access/device_access.kshlib

check_cap_support

save_perms $DEV1
save_perms $DEV2

typeset tmpcache=$(mktemp)

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_perms $DEV1
	restore_perms $DEV2
	rm -f $tmpcache
}
log_onexit cleanup

log_assert 'device permissions are properly checked for zpool import'

# create a pool with two devices, capture the cache file, and export it
log_must chmod 666 $DEV1 $DEV2
log_must zpool create $TESTPOOL $DEV1 $DEV2
check_vdevs $TESTPOOL $DEV1 $DEV2
cp /etc/zfs/zpool.cache $tmpcache
log_must zpool export $TESTPOOL

# remove perms from devices, check pool can be imported. this is relying on the
# running user having CAP_DAC_OVERRIDE to bypass the permission check
log_must chmod 000 $DEV1 $DEV2
log_must zpool import $TESTPOOL
check_vdevs $TESTPOOL $DEV1 $DEV2
log_must zpool export $TESTPOOL

# try to import the pool without CAP_DAC_OVERRIDE. this should fail, as the
# user has no direct permission and has no override capability.
log_mustnot without_cap cap_dac_override zpool import $TESTPOOL
log_mustnot poolexists $TESTPOOL

# make one device accessible, and try to import again. should fail, because
# the userspace device scan can't access all the devices
log_must chmod 666 $DEV1
log_mustnot without_cap cap_dac_override zpool import $TESTPOOL
log_mustnot poolexists $TESTPOOL

# try to import using the cachefile. should fail, because one of the devices
# listed in the cachefile is not accessible to the user
log_mustnot without_cap cap_dac_override zpool import -c $tmpcache $TESTPOOL
log_mustnot poolexists $TESTPOOL

log_pass 'device permissions are properly checked for zpool import'
