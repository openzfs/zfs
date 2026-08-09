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
# Write a file to the allowable ZFS fs size.
#
# STRATEGY:
# 1. largest_file will write to a file and increase its size
# to the maximum allowable.
# 2. The last byte of the file should be accessible without error.
# 3. Writing beyond the maximum file size generates an 'errno' of
# EFBIG.
#

verify_runnable "both"

log_assert "Write a file to the allowable ZFS fs size."

log_note "Invoke 'largest_file' with $TESTDIR/bigfile"
log_must largest_file $TESTDIR/bigfile

log_pass "Successfully created a file to the maximum allowable size."
