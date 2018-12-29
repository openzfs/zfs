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
