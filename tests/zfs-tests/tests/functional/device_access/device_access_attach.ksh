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

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_perms $DEV1
	restore_perms $DEV2
}
log_onexit cleanup

log_assert 'device permissions are properly checked for zpool attach'

# make device accessible to everyone, ensure pool can be created and devices
# attached.
log_must chmod 666 $DEV1 $DEV2
log_must zpool create $TESTPOOL $DEV1
log_must zpool attach $TESTPOOL $DEV1 $DEV2
check_vdevs $TESTPOOL $DEV1 $DEV2
log_must zpool destroy $TESTPOOL

# remove perms from devices, check pool can be created and device attached.
# this is relying on the running user having CAP_DAC_OVERRIDE to bypass the
# permission check
log_must chmod 000 $DEV1 $DEV2
log_must zpool create $TESTPOOL $DEV1
log_must zpool attach $TESTPOOL $DEV1 $DEV2
check_vdevs $TESTPOOL $DEV1 $DEV2
log_must zpool destroy $TESTPOOL

# recreate the pool, then try to attach without CAP_DAC_OVERRIDE. this should
# fail, as the user has no direct permission and has no override capability.
log_must zpool create $TESTPOOL $DEV1
log_mustnot without_cap cap_dac_override zpool attach $TESTPOOL $DEV1 $DEV2

# confirm that the device was really not attached, not just that the call failed
check_vdevs $TESTPOOL $DEV1

log_must zpool destroy $TESTPOOL

log_pass 'device permissions are properly checked for zpool attach'
