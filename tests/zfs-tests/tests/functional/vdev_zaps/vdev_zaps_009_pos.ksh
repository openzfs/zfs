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
# Copyright (c) 2026, OpenZFS contributors.
#
# Exercise root-ZAP activation with missing or unavailable vdevs in
# disposable file-backed ztest pools.  No physical disks are used.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

ZTEST_AVZ_DIR="$TEST_BASE_DIR/ztest-avz-$$"
function cleanup
{
	rm -rf -- "$ZTEST_AVZ_DIR"
}
log_onexit cleanup

log_assert "Root ZAP activation defers until recovery and stays active"
log_must mkdir -p "$ZTEST_AVZ_DIR"
log_must ztest --vdev-zaps -s 64M -p "ztestavz$$" -f "$ZTEST_AVZ_DIR"
log_pass "Root ZAP deferral, recovery and active-state checks passed"
