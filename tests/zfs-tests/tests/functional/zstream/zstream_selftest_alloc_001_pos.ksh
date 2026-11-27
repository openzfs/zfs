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

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Run zstream's built-in unit tests ("zstream selftest") for allocator_t.
#
# Strategy:
# 1. Run all selftests for the alloc module, with disk-backed allocators
#    spilling to the test file system.
#

verify_runnable "both"

log_assert "zstream self-tests for allocator_t all pass"

export ZSTREAM_SELFTEST_DIR=$TESTDIR
log_must zstream selftest alloc

log_pass "zstream self-tests for allocator_t all pass"
