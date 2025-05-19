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

log_must zpool create $TESTPOOL anymirror3 $disks
log_must poolexists $TESTPOOL
log_must zpool export $TESTPOOL

poolexists $TESTPOOL && \
        log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully exported an AnyRAID pool."
