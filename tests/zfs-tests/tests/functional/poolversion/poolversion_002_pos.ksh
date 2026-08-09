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
