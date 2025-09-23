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
# Verify that creating an anyraidz pool with a data width of 0
# produces a clear error and does not crash or create a pool.
# Note: anyraidz1:0 is already tested in 005_neg. This test provides
# additional coverage with different parity levels.
#
# STRATEGY:
# 1. Create sparse file vdevs.
# 2. Attempt to create anyraidz pools with data width of 0 at
#    each valid parity level (1, 2, 3).
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

log_assert "anyraidz with data width of 0 must fail"
log_onexit cleanup

create_sparse_files "disk" 4 $MINVDEVSIZE2

#
# anyraidz1:0 - parity 1 with 0 data width must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz1:0 $disks
log_mustnot poolexists $TESTPOOL

#
# anyraidz2:0 - parity 2 with 0 data width must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz2:0 $disks
log_mustnot poolexists $TESTPOOL

#
# anyraidz3:0 - parity 3 with 0 data width must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz3:0 $disks
log_mustnot poolexists $TESTPOOL

#
# Verify the error output is not a panic or stack trace.
#
errmsg=$(zpool create $TESTPOOL anyraidz2:0 $disks 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_mustnot poolexists $TESTPOOL

log_pass "anyraidz with data width of 0 must fail"
