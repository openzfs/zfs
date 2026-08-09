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
# zfs upgrade returns an error when run as a user
#
# STRATEGY:
# 1. Attempt to upgrade a version1 dataset
# 2. Verify the dataset wasn't upgraded
#
#

# check to see if we have upgrade capability
zfs upgrade > /dev/null 2>&1
HAS_UPGRADE=$?
if [ $HAS_UPGRADE -ne 0 ]
then
	log_unsupported "Zfs upgrade not supported"
fi

log_assert "zfs upgrade returns an error when run as a user"


log_mustnot zfs upgrade $TESTPOOL/$TESTFS/version1

# now check to see the above command didn't do anything
VERSION=$(zfs upgrade $TESTPOOL/$TESTFS/version1 2>&1 \
	 | grep "already at this version")
if [ -n "$VERSION" ]
then
	log_fail "A filesystem was upgraded!"
fi

log_pass "zfs upgrade returns an error when run as a user"
