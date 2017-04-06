#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Writing to a file and mmaping that file at the
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
is_mp || log_fail "This test requires a multi-processor system."

log_must chmod 777 $TESTDIR
mmapwrite $TESTDIR/test-write-file &
PID_MMAPWRITE=$!
log_note "mmapwrite $TESTDIR/test-write-file pid: $PID_MMAPWRITE"
log_must sleep 30

log_must kill -9 $PID_MMAPWRITE
log_must ls -l $TESTDIR/test-write-file

log_pass "write(2) a mmap(2)'ing file succeeded."
