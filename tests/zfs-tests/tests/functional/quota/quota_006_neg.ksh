#! /bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/quota/quota.kshlib

#
# DESCRIPTION:
#
# Can't set a quota to less than currently being used by the dataset.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Set a quota on the filesystem that is lower than the space
#	currently in use.
# 3) Verify that the attempt fails.
#

verify_runnable "both"

log_assert "Verify cannot set quota lower than the space currently in use"

function cleanup
{
	reset_quota $TESTPOOL/$TESTFS
}

log_onexit cleanup

typeset -i quota_integer_size=0
typeset invalid_size="123! @456 7#89 0\$ abc123% 123%s 12%s3 %c123 123%d %x123 12%p3 \
	^def456 789&ghi"
typeset -i space_used=`get_prop used $TESTPOOL/$TESTFS`
(( quota_integer_size = space_used  - 1 ))
quota_fp_size=${quota_integer_size}.123

for size in 0 -1 $quota_integer_size -$quota_integer_size $quota_fp_size -$quota_fp_size \
	$invalid_size ; do
	log_mustnot zfs set quota=$size $TESTPOOL/$TESTFS
done
log_must zfs set quota=$space_used $TESTPOOL/$TESTFS

log_pass "As expected cannot set quota lower than space currently in use"
