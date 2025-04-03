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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set version can only increment pool version
#
# STRATEGY:
# 1. Set a version 1 pool to be a version 6 pool
# 2. Verify it's set to version 6
# 3. Attempt to set prior versions
# 4. Verify it's still set to version 6
#

verify_runnable "global"
log_assert "zpool set version can only increment pool version"

log_must zpool set version=6 $TESTPOOL2
# verify it's actually that version - by checking the version property
# and also by trying to set bootfs (which should fail if it is not version 6)

VERSION=$(get_pool_prop version $TESTPOOL2)
if [ "$VERSION" != "6" ]
then
	log_fail "Version $VERSION set for $TESTPOOL2 expected version 6!"
fi
log_must zpool set bootfs=$TESTPOOL2 $TESTPOOL2

# now verify we can't downgrade the version
log_mustnot zpool set version=5 $TESTPOOL2
log_mustnot zpool set version=-1 $TESTPOOL2

# verify the version is still 6
VERSION=$(get_pool_prop version $TESTPOOL2)
if [ "$VERSION" != "6" ]
then
	log_fail "Version $VERSION set for $TESTPOOL2, expected version 6!"
fi

log_pass "zpool set version can only increment pool version"
