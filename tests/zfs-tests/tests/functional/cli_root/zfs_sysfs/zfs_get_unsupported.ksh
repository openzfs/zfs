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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A property not supported by the zfs module should fail in 'zfs get <prop>'
#
# STRATEGY:
# 1. Run zfs get <prop> with the env variable 'ZFS_SYSFS_PROP_SUPPORT_TEST'
# 2. Verify that zfs get returns error
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "sysfs is linux-only"
fi

claim="Properties not supported by zfs module should fail in 'zfs get <prop>'"

unsupported_prop="dnodesize"

log_assert $claim

log_mustnot eval "ZFS_SYSFS_PROP_SUPPORT_TEST=yes zfs get ${unsupported_prop} \
	$TESTPOOL/$TESTFS >/dev/null 2>&1"

log_pass $claim
