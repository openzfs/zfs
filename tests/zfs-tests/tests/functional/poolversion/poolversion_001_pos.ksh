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
# zpool set version can upgrade a pool
#
# STRATEGY:
# 1. Taking a version 1 pool
# 2. For all known versions, set the version of the pool using zpool set
# 3. Verify that pools version
#

verify_runnable "global"
log_assert "zpool set version can upgrade a pool"
for version in 1 2 3 4 5 6 7 8
do
	log_must zpool set version=$version $TESTPOOL
	ACTUAL=$(get_pool_prop version $TESTPOOL)
	if [ "$ACTUAL" != "$version" ]
	then
		log_fail "v. $ACTUAL set for $TESTPOOL, expected v. $version!"
	fi
done

log_pass "zpool set version can upgrade a pool"
