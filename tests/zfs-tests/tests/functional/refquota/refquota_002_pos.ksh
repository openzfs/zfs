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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Quotas are enforced using the minimum of the two properties:
#	quota & refquota
#
# STRATEGY:
#	1. Set value for quota and refquota. Quota less than refquota.
#	2. Creating file which should be limited by quota.
#	3. Switch the value of quota and refquota.
#	4. Verify file should be limited by refquota.
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Quotas are enforced using the minimum of the two properties"
log_onexit cleanup

TESTFILE='testfile'
fs=$TESTPOOL/$TESTFS
log_must zfs set quota=15M $fs
log_must zfs set refquota=25M $fs

mntpnt=$(get_prop mountpoint $fs)
log_mustnot mkfile 20M $mntpnt/$TESTFILE
typeset -i used quota
used=$(get_prop used $fs)
quota=$(get_prop quota $fs)
((used = used / (1024 * 1024)))
((quota = quota / (1024 * 1024)))
if [[ $used -ne $quota ]]; then
	log_fail "ERROR: $used -ne $quota Quotas are not limited by quota"
fi

#
# Switch the value of them and try again
#
log_must rm $mntpnt/$TESTFILE
log_must zfs set quota=25M $fs
log_must zfs set refquota=15M $fs

log_mustnot mkfile 20M $mntpnt/$TESTFILE
used=$(get_prop used $fs)
refquota=$(get_prop refquota $fs)
((used = used / (1024 * 1024)))
((refquota = refquota / (1024 * 1024)))
if [[ $used -ne $refquota ]]; then
	log_fail "ERROR: $used -ne $refquota Quotas are not limited by refquota"
fi

log_pass "Quotas are enforced using the minimum of the two properties"
