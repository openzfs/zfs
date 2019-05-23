#! /bin/ksh -p
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
# Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify write(2) to SUID/SGID file by non-owner.
# Also see https://github.com/pjd/pjdfstest/blob/master/tests/chmod/12.t
#
# STRATEGY:
# 1. creat(2) a file with SUID/SGID.
# 2. write(2) to the file with uid=65534.
# 3. stat(2) the file and verify .st_mode value.
#

verify_runnable "both"

function cleanup
{
	rm -f $TESTDIR/$TESTFILE0
}

log_onexit cleanup
log_note "Verify write(2) to SUID/SGID file by non-owner"

log_must $STF_SUITE/tests/functional/suid/suid_write_to_file "SUID_SGID"

log_pass "Verify write(2) to SUID/SGID file by non-owner passed"
