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
