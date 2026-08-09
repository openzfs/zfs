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

#
# DESCRIPTION:
#	'zfs get' should get consistent report with different options.
#
# STRATEGY:
#	1. Create pool and filesystem.
#	2. 'zfs mount -o remount,noatime <fs>.'
#	3. Verify the value of 'zfs get atime' and 'zfs get all | grep atime'
#	   are identical.
#

verify_runnable "both"

function cleanup
{
	log_must zfs mount -o remount,atime $TESTPOOL/$TESTFS
}

log_assert "'zfs get' should get consistent report with different option."
log_onexit cleanup

log_must zfs set atime=on $TESTPOOL/$TESTFS
log_must zfs mount -o remount,noatime $TESTPOOL/$TESTFS

read -r _ _ value1 _ < <(zfs get -H atime $TESTPOOL/$TESTFS)
read -r _ value2 < <(zfs get -H all $TESTPOOL/$TESTFS | cut -f2,3 | grep ^atime)
if [[ $value1 != $value2 ]]; then
	log_fail "value1($value1) != value2($value2)"
fi

log_pass "'zfs get'  get consistent report with different option passed."
