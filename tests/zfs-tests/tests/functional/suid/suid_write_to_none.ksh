#! /bin/ksh -p
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
# Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify write(2) to regular file by non-owner.
# Also see https://github.com/pjd/pjdfstest/blob/master/tests/chmod/12.t
#
# STRATEGY:
# 1. creat(2) a file.
# 2. write(2) to the file with uid=65534.
# 3. stat(2) the file and verify .st_mode value.
#

verify_runnable "both"

function cleanup
{
	rm -f $TESTDIR/$TESTFILE0
}

log_onexit cleanup
log_note "Verify write(2) to regular file by non-owner"

log_must suid_write_to_file "NONE" "PRECRASH"

log_pass "Verify write(2) to regular file by non-owner passed"
