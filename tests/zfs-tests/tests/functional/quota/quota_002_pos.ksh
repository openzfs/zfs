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
# A zfs file system quota limits the amount of pool space
# available to a given ZFS file system. Once exceeded, it is impossible
# to write any more files to the file system.
#
# STRATEGY:
# 1) Apply quota to the ZFS file system
# 2) Exceed the quota
# 3) Attempt to write another file
# 4) Verify the attempt fails with error code EDQUOTA (linux 122, others 49)
#
#

verify_runnable "both"

log_assert "Verify that a file write cannot exceed the file system quota"

#
# cleanup to be used internally as otherwise quota assertions cannot be
# run independently or out of order
#
function cleanup
{
        [[ -e $TESTDIR/$TESTFILE1 ]] && \
            log_must rm $TESTDIR/$TESTFILE1

	[[ -e $TESTDIR/$TESTFILE2 ]] && \
            log_must rm $TESTDIR/$TESTFILE2

	wait_freeing $TESTPOOL
	sync_pool $TESTPOOL
}

log_onexit cleanup

#
# Fills the quota & attempts to write another file
#
log_must exceed_quota $TESTPOOL/$TESTFS $TESTDIR

log_pass "Could not write file. Quota limit enforced as expected"
