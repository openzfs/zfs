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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# An AnyRAID pool should be exportable and not visible from 'zpool list'.
#
# STRATEGY:
# 1. Create AnyRAID pool.
# 2. Export the pool.
# 3. Verify the pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify an AnyRAID pool can be exported."
log_onexit cleanup

poolexists $TESTPOOL && destroy_pool $TESTPOOL

create_sparse_files "disk" 4 $MINVDEVSIZE2

log_must zpool create $TESTPOOL anyraid3 $disks
log_must poolexists $TESTPOOL
log_must zpool export $TESTPOOL

poolexists $TESTPOOL && \
        log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully exported an AnyRAID pool."
