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
