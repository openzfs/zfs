#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
