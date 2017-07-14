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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Sub-filesystem quotas are not enforced by property 'refquota'
#
# STRATEGY:
#	1. Setting quota and refquota for parent. refquota < quota
#	2. Verify sub-filesystem will not be limited by refquota
#	3. Verify sub-filesystem will only be limited by quota
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Sub-filesystem quotas are not enforced by property 'refquota'"
log_onexit cleanup

TESTFILE='testfile'
fs=$TESTPOOL/$TESTFS
log_must zfs set quota=25M $fs
log_must zfs set refquota=10M $fs
log_must zfs create $fs/subfs

mntpnt=$(get_prop mountpoint $fs/subfs)
log_must mkfile 20M $mntpnt/$TESTFILE

typeset -i used quota refquota
used=$(get_prop used $fs)
refquota=$(get_prop refquota $fs)
((used = used / (1024 * 1024)))
((refquota = refquota / (1024 * 1024)))
if [[ $used -lt $refquota ]]; then
	log_fail "ERROR: $used < $refquota subfs quotas are limited by refquota"
fi

log_mustnot mkfile 20M $mntpnt/$TESTFILE.2
used=$(get_prop used $fs)
quota=$(get_prop quota $fs)
((used = used / (1024 * 1024)))
((quota = quota / (1024 * 1024)))
if [[ $used -gt $quota ]]; then
	log_fail "ERROR: $used > $quota subfs quotas aren't limited by quota"
fi

log_pass "Sub-filesystem quotas are not enforced by property 'refquota'"
