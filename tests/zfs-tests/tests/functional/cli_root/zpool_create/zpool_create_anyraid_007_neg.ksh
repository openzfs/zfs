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
# Verify that creating an anyraidz pool where the data width exceeds
# the available disk count produces a clear error and does not crash
# or create a pool.
#
# STRATEGY:
# 1. Create sparse file vdevs.
# 2. Attempt to create anyraidz pools where parity + data width
#    exceeds the number of disks provided.
# 3. Verify each attempt fails with log_mustnot.
# 4. Verify no pool was created.
# 5. Verify stderr output is meaningful (not a panic or stack trace).
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	delete_sparse_files
}

log_assert "anyraidz with data width larger than disk count must fail"
log_onexit cleanup

create_sparse_files "disk" 3 $MINVDEVSIZE2

#
# anyraidz1:4 requires 1 parity + 4 data = 5 disks minimum.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz1:4 $disk0 $disk1 $disk2
log_note "DEBUG: anyraidz1:4 with 3 disks failed as expected"

#
# anyraidz1:3 requires 1 parity + 3 data = 4 disks minimum.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz1:3 $disk0 $disk1 $disk2
log_mustnot poolexists $TESTPOOL

#
# anyraidz2:3 requires 2 parity + 3 data = 5 disks minimum.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz2:3 $disk0 $disk1 $disk2
log_mustnot poolexists $TESTPOOL

#
# anyraidz3:2 requires 3 parity + 2 data = 5 disks minimum.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anyraidz3:2 $disk0 $disk1 $disk2
log_mustnot poolexists $TESTPOOL

#
# Verify the error output is not a panic or stack trace.
#
log_note "DEBUG: verifying error message is meaningful for anyraidz1:4 with 3 disks"
errmsg=$(zpool create $TESTPOOL anyraidz1:4 $disk0 $disk1 $disk2 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_mustnot poolexists $TESTPOOL

log_pass "anyraidz with data width larger than disk count must fail"
