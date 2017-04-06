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

# check to see if we have zfs unallow
zfs 2>&1 | grep "unallow" > /dev/null
if (($? != 0)) then
        log_unsupported "ZFS unallow not supported on this machine."
fi

log_assert "zfs unallow returns an error when run as a user"

log_mustnot zfs unallow everyone $TESTPOOL/$TESTFS/allowed

# now check with zfs allow to see if the permissions are still there
OUTPUT=$(zfs allow $TESTPOOL/$TESTFS/allowed | grep "Local+Descendent" )
if [ -z "$OUTPUT" ]
then
	log_fail "Error - create permissions were unallowed on \
	$TESTPOOL/$TESTFS/allowed"
fi

log_pass "zfs unallow returns an error when run as a user"
