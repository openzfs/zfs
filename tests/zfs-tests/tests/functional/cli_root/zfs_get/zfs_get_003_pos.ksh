#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
