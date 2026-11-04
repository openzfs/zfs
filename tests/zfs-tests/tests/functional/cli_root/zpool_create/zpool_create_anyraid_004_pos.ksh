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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# Verify that AnyRAID vdevs of different sizes can be mixed in a pool
#
# STRATEGY:
# 1. Create a pool with two anyraid vdevs with different disk counts
# 2. Verify the pool created successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Pools can have multiple anyraid children with different disk counts"
log_onexit cleanup

create_sparse_files "disk" 5 $MINVDEVSIZE2

# Verify the default parity
log_must zpool create $TESTPOOL anymirror $disk0 $disk1 $disk2 anymirror $disk3 $disk4
log_must poolexists $TESTPOOL
destroy_pool $TESTPOOL

log_pass "Pools can have multiple anyraid children with different disk counts."
