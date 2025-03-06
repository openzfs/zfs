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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Writing to a file and mmapping that file at the
# same time does not result in a deadlock.
#
# STRATEGY:
# 1. Make sure this test executes on multi-processes system.
# 2. Call mmapwrite binary.
# 3. wait 30s and make sure the test file existed.
#

verify_runnable "both"

log_assert "write()s to a file and mmap() that file at the same time does not "\
	"result in a deadlock."

# Detect and make sure this test must be executed on a multi-process system
if ! is_mp; then
	log_unsupported "This test requires a multi-processor system."
fi

log_must chmod 777 $TESTDIR
mmapwrite $TESTDIR/normal_write_file $TESTDIR/map_write_file &
PID_MMAPWRITE=$!
log_note "mmapwrite $TESTDIR/normal_write_file $TESTDIR/map_write_file"\
	 "pid: $PID_MMAPWRITE"
log_must sleep 30

log_must kill -9 $PID_MMAPWRITE
log_must ls -l $TESTDIR/normal_write_file
log_must ls -l $TESTDIR/map_write_file

log_pass "write(2) a mmap(2)'ing file succeeded."
