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
# Verify that creating an anymirror pool with too few disks for
# the requested parity level produces a clear error and does not
# crash or create a pool.
#
# STRATEGY:
# 1. Create sparse file vdevs.
# 2. Attempt to create pools where the disk count is less than
#    or equal to the parity level (need parity+1 disks minimum).
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

log_assert "anymirror with too few disks for parity level must fail"
log_onexit cleanup

create_sparse_files "disk" 3 $MINVDEVSIZE2

#
# anymirror1 requires at least 2 disks.
# With only 1 disk it must fail.
#
log_mustnot zpool create $TESTPOOL anymirror1 $disk0
log_mustnot poolexists $TESTPOOL

#
# anymirror2 requires at least 3 disks.
# With only 2 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anymirror2 $disk0 $disk1
log_mustnot poolexists $TESTPOOL

#
# anymirror3 requires at least 4 disks.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anymirror3 $disk0 $disk1 $disk2
log_mustnot poolexists $TESTPOOL

#
# anymirror6 requires at least 7 disks.
# With only 3 disks it must fail.
#
log_mustnot zpool create $TESTPOOL anymirror6 $disk0 $disk1 $disk2
log_mustnot poolexists $TESTPOOL

#
# Verify the error output is not a panic or stack trace.
#
log_note "DEBUG: verifying error message is meaningful for anymirror3 with 3 disks"
errmsg=$(zpool create $TESTPOOL anymirror3 $disk0 $disk1 $disk2 2>&1)
log_note "DEBUG: error output was: $errmsg"
if echo "$errmsg" | grep -qi "panic\|stack\|dump\|segfault"; then
	log_fail "Error output contains panic/crash indicators: $errmsg"
fi
log_note "DEBUG: error message verified as meaningful (no panic/crash)"
log_mustnot poolexists $TESTPOOL

log_pass "anymirror with too few disks for parity level must fail"
