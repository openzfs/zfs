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
