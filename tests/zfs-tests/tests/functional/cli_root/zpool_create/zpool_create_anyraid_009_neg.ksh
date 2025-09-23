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
# Verify that creating an anyraidz pool with a parity level exceeding
# 3 produces a clear error and does not crash or create a pool.
# Valid anyraidz parity levels are 1, 2, and 3 only.
#
# STRATEGY:
# 1. Create sparse file vdevs.
# 2. Attempt to create anyraidz pools with parity levels 4, 5, 6
#    and other invalid values.
# 3. Verify each attempt fails with log_mustnot.
# 4. Verify no pool was created.
# 5. Verify stderr output is meaningful (not a panic or stack trace).
#

verify_runnable "global"

function cleanup
{
	log_note "DEBUG: cleanup - destroying pool if it exists"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_note "DEBUG: cleanup - deleting sparse files"
	delete_sparse_files
}

log_assert "anyraidz with parity level exceeding 3 must fail"
log_onexit cleanup

create_sparse_files "disk" 7 $MINVDEVSIZE2

#
# anyraidz4:1 - parity 4 is not valid for anyraidz.
#
log_mustnot zpool create $TESTPOOL anyraidz4:1 $disks
log_mustnot poolexists $TESTPOOL

#
# anyraidz5:1 - parity 5 is not valid for anyraidz.
#
log_mustnot zpool create $TESTPOOL anyraidz5:1 $disks
log_mustnot poolexists $TESTPOOL

#
# anyraidz6:1 - parity 6 is not valid for anyraidz.
#
log_mustnot zpool create $TESTPOOL anyraidz6:1 $disks
log_mustnot poolexists $TESTPOOL

#
# anyraidz0:2 - parity 0 is not valid for anyraidz.
#
log_mustnot zpool create $TESTPOOL anyraidz0:2 $disks
log_mustnot poolexists $TESTPOOL

#
# Verify the error output is not a panic or stack trace.
#
errmsg=$(zpool create $TESTPOOL anyraidz4:1 $disks 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_mustnot poolexists $TESTPOOL

log_pass "anyraidz with parity level exceeding 3 must fail"
