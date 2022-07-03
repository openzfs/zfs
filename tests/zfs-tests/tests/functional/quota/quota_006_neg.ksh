#! /bin/ksh -p
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
	log_must zfs set quota=none $TESTPOOL/$TESTFS
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
