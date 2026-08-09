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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
#
# DESCRIPTION:
#	Check 'zfs project' is compatible with chattr/lsattr
#
#
# STRATEGY:
#	Verify the following:
#	1. "zfs project -p" behaviours the same as "chattr -p"
#	2. "zfs project" behaviours the same as "lsattr -p"
#	3. "zfs project -d" behaviours the same as "lsattr -p -d"
#	4. "zfs project -s" behaviours the same as "chattr +P"
#	5. "zfs project -s -p" behaviours the same as "chattr +P -p"
#	6. "zfs project -C" behaviours the same as "chattr -P"
#

function cleanup
{
	log_must rm -rf $PRJDIR
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

#
# e2fsprogs-1.44.4 incorrectly reports verity 'V' bit when the project 'P'
# bit is set.  Skip this test when 1.44.4 is installed to prevent failures.
#
# https://github.com/tytso/e2fsprogs/commit/7e5a95e3d
#
if lsattr -V 2>&1 | grep "lsattr 1.44.4"; then
	log_unsupported "Current e2fsprogs incorrectly reports 'V' verity bit"
fi

log_onexit cleanup

log_assert "Check 'zfs project' is compatible with chattr/lsattr"

log_must mkdir $PRJDIR
log_must mkdir $PRJDIR/a1
log_must mkdir $PRJDIR/a2
log_must touch $PRJDIR/a3

log_must chattr -p $PRJID1 $PRJDIR/a3
log_must eval "zfs project $PRJDIR/a3 | grep '$PRJID1 \-'"

log_must zfs project -p $PRJID2 $PRJDIR/a3
log_must eval "lsattr -p $PRJDIR/a3 | grep $PRJID2 | grep -v '\-P[- ]* '"

log_must chattr -p $PRJID1 $PRJDIR/a1
log_must eval "zfs project -d $PRJDIR/a1 | grep '$PRJID1 \-'"

log_must zfs project -p $PRJID2 $PRJDIR/a1
log_must eval "lsattr -pd $PRJDIR/a1 | grep $PRJID2 | grep -v '\-P[- ]* '"

log_must chattr +P $PRJDIR/a2
log_must eval "zfs project -d $PRJDIR/a2 | grep '0 P'"

log_must zfs project -s $PRJDIR/a2
log_must eval "lsattr -pd $PRJDIR/a2 | grep 0 | grep '\-P[- ]* '"

log_must chattr +P -p $PRJID1 $PRJDIR/a1
log_must eval "zfs project -d $PRJDIR/a1 | grep '$PRJID1 P'"

log_must zfs project -s -p $PRJID2 $PRJDIR/a2
log_must eval "lsattr -pd $PRJDIR/a2 | grep $PRJID2 | grep '\-P[- ]* '"

log_must chattr -P $PRJDIR/a1
log_must eval "zfs project -d $PRJDIR/a1 | grep '$PRJID1 \-'"

log_must zfs project -C -k $PRJDIR/a2
log_must eval "lsattr -pd $PRJDIR/a2 | grep $PRJID2 | grep -v '\-P[- ]* '"

log_pass "Check 'zfs project' is compatible with chattr/lsattr"
