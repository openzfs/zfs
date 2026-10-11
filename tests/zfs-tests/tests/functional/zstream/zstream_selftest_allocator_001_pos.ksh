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
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Run zstream's built-in unit tests ("zstream selftest").
#
# Strategy:
# 1. Create a dataset to hold the selftests' spill files, so that the tests
#    write only within the test pool, and on ZFS.
# 2. Run all selftests for the allocator module.
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $POOL/selftest "-r"
}
log_onexit cleanup

log_assert "zstream self-tests for zstream_alloc all pass"

log_must zfs create $POOL/selftest
export ZSTREAM_SELFTEST_DIR=$(get_prop mountpoint $POOL/selftest)
log_must zstream selftest allocator

log_pass "zstream self-tests for zstream_alloc all pass"
