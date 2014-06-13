#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zfs allow returns an error when run as a user
#
# STRATEGY:
#
# 1. Verify that trying to show allows works as a user
# 2. Verify that trying to set allows fails as a user
#
#

# check to see if we have zfs allow
$ZFS 2>&1 | $GREP "allow" > /dev/null
if (($? != 0)) then
	log_unsupported "ZFS allow not supported on this machine."
fi

log_assert "zfs allow returns an error when run as a user"

log_must $ZFS allow $TESTPOOL/$TESTFS
log_mustnot $ZFS allow $($LOGNAME) create $TESTPOOL/$TESTFS

# now verify that the above command actually did nothing by
# checking for any allow output. ( if no allows are granted,
# nothing should be output )
OUTPUT=$($ZFS allow $TESTPOOL/$TESTFS | $GREP "Local+Descendent" )
if [ -n "$OUTPUT" ]
then
	log_fail "zfs allow permissions were granted on $TESTPOOL/$TESTFS"
fi

log_pass "zfs allow returns an error when run as a user"
