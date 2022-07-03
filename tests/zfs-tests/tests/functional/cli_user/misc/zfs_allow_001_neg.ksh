#!/bin/ksh -p
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
# zfs allow returns an error when run as a user
#
# STRATEGY:
#
# 1. Verify that trying to show allows works as a user
# 2. Verify that trying to set allows fails as a user
#
#

log_assert "zfs allow returns an error when run as a user"

log_must zfs allow $TESTPOOL/$TESTFS
log_mustnot zfs allow $(id -un) create $TESTPOOL/$TESTFS

# now verify that the above command actually did nothing by
# checking for any allow output. ( if no allows are granted,
# nothing should be output )
if zfs allow $TESTPOOL/$TESTFS | grep -q "Local+Descendent"
then
	log_fail "zfs allow permissions were granted on $TESTPOOL/$TESTFS"
fi

log_pass "zfs allow returns an error when run as a user"
