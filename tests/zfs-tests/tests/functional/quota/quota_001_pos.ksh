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
# A ZFS file system quota limits the amount of pool space
# available to a file system. Apply a quota and verify
# that no more file creates are permitted.
#
# STRATEGY:
# 1) Apply quota to ZFS file system
# 2) Create a file which is larger than the set quota
# 3) Verify that the resulting file size is less than the quota limit
#

verify_runnable "both"

log_assert "Verify that file size is limited by the file system quota"

#
# cleanup to be used internally as otherwise quota assertions cannot be
# run independently or out of order
#
function cleanup
{
	[[ -e $TESTDIR/$TESTFILE1 ]] && \
	    log_must rm $TESTDIR/$TESTFILE1
	#
	# Need to allow time for space to be released back to
	# pool, otherwise next test will fail trying to set a
	# quota which is less than the space used.
	#
	wait_freeing $TESTPOOL
	sync_pool $TESTPOOL

	reset_quota $TESTPOOL/$TESTFS
}

log_onexit cleanup

#
# Sets the quota value and attempts to fill it with a file
# twice the size of the quota
#
log_must fill_quota $TESTPOOL/$TESTFS $TESTDIR

log_pass "File size limited by quota"
