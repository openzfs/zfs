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
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zfs unallow returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to unallow a set of permissions
# 2. Verify the unallow wasn't performed
#
#

log_assert "zfs unallow returns an error when run as a user"

log_mustnot zfs unallow everyone $TESTPOOL/$TESTFS/allowed

# now check with zfs allow to see if the permissions are still there
if ! zfs allow $TESTPOOL/$TESTFS/allowed | grep -q "Local+Descendent"
then
	log_fail "Error - create permissions were unallowed on \
	$TESTPOOL/$TESTFS/allowed"
fi

log_pass "zfs unallow returns an error when run as a user"
