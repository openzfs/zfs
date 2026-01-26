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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zpool contract' with a non-existent pool name
# produces an error and does not crash.
#
# STRATEGY:
# 1. Attempt 'zpool contract' with a pool name that does not exist.
# 2. Verify the command fails.
# 3. Verify the error output is meaningful (not a panic).
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - nothing to clean"
}

log_assert "'zpool contract' with non-existent pool must fail"
log_onexit cleanup

#
# Use a pool name that definitely does not exist
#
log_note "DEBUG: testing 'zpool contract' with non-existent pool"
log_mustnot zpool contract nonexistent_pool_xyz anymirror1-0 /dev/null

#
# Verify the error output is meaningful
#
log_note "DEBUG: verifying error message is meaningful"
errmsg=$(zpool contract nonexistent_pool_xyz anymirror1-0 /dev/null 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_note "DEBUG: error message verified as meaningful (no panic/crash)"

log_pass "'zpool contract' with non-existent pool must fail"
