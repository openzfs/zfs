#!/bin/ksh -p
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
