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
# Verify that mixing anymirror and anyraidz keywords in the same
# vdev group produces a clear error and does not crash or create
# a pool. Each anyraid vdev group must use a single type.
#
# STRATEGY:
# 1. Create sparse file vdevs.
# 2. Attempt to create pools that combine anymirror and anyraidz
#    in a single vdev specification (not as separate top-level vdevs).
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

log_assert "Mixing anymirror and anyraidz in the same vdev group must fail"
log_onexit cleanup

create_sparse_files "disk" 6 $MINVDEVSIZE2

#
# Attempt to specify both anymirror and anyraidz as if they were
# the same vdev group by placing them adjacent without separation.
# The parser should reject this.
#
log_mustnot zpool create $TESTPOOL anymirror1 anyraidz1:1 $disks
log_mustnot poolexists $TESTPOOL

#
# Attempt with anyraidz first, then anymirror.
#
log_mustnot zpool create $TESTPOOL anyraidz1:1 anymirror1 $disks
log_mustnot poolexists $TESTPOOL

#
# Attempt with both keywords back-to-back before disks, reversed order.
#
log_mustnot zpool create $TESTPOOL anyraidz1:2 anymirror1 $disks
log_mustnot poolexists $TESTPOOL

#
# Attempt with both keywords back-to-back, different parity.
#
log_mustnot zpool create $TESTPOOL anymirror2 anyraidz2:1 $disks
log_mustnot poolexists $TESTPOOL

#
# Verify the error output is not a panic or stack trace for one
# of the clearly invalid cases.
#
log_note "DEBUG: verifying error message is meaningful"
errmsg=$(zpool create $TESTPOOL anymirror1 anyraidz1:1 $disks 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_mustnot poolexists $TESTPOOL

log_pass "Mixing anymirror and anyraidz in the same vdev group must fail"
