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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Run zstream's built-in unit tests ("zstream selftest").
#
# Strategy:
# 1. Run all selftests for each module with the default worker thread pool.
# 2. Run them again with a single worker thread.
#

verify_runnable "both"

log_assert "zstream self-tests for zstream_queue all pass"

log_must zstream selftest queue
log_must zstream selftest -t 1 queue

log_pass "zstream self-tests for zstream_queue all pass"
