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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zfs rollback returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to rollback a snapshot
# 2. Verify that a file which doesn't exist in the snapshot still exists
#    (showing the snapshot rollback failed)
#
#

log_assert "zfs rollback returns an error when run as a user"

log_mustnot zfs rollback $TESTPOOL/$TESTFS@snap

# now verify the above command didn't actually do anything

# in the above filesystem there's a file that should not exist once
# the snapshot is rolled back - we check for it
if [ ! -e /$TESTDIR/file.txt ]
then
	log_fail "Rollback of snapshot $TESTPOOL/$TESTFS@snap succeeded!"
fi

log_pass "zfs rollback returns an error when run as a user"
