#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A scrub can use a DTL-marked replacement copy for recovery during a rebuild
# without letting an unverified column read overwrite the original.
#
# STRATEGY:
# 1. Hold a userspace dRAID rebuild and install a copied column on the
#    replacement, retaining its DTL.
# 2. Fail the original's reads and another column's reads before dispatch.
# 3. Require a scrub read to recover and the verified parent to repair.
# 4. Corrupt the replacement's column and require the parent to reject it
#    without rewriting the original from that unverified copy.
#

verify_runnable "global"

function cleanup
{
	rm -rf "$workdir"
}

log_assert "Scrub fallback preserves recovery and requires verified repairs"
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_rebuild_recovery.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must scrub_rebuild_recovery "$workdir"

log_pass "Scrub fallback preserves recovery and requires verified repairs"
