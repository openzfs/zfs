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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test fallocate(2) preallocation.
#
# STRATEGY:
# 1. Verify mode 0 fallocate is supported.
# 2. Verify default 10% reserve space is honored by setting a quota.
#

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0

function cleanup
{
	log_must zfs set quota=none $TESTPOOL

	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

log_assert "Ensure sparse files can be preallocated"

log_onexit cleanup

# Pre-allocate a sparse 1GB file.
log_must fallocate -l $((1024 * 1024 * 1024)) $FILE
log_must rm -Rf $TESTDIR/*

# Verify that an additional ~10% reserve space is required.
log_must zfs set quota=100M $TESTPOOL
log_mustnot fallocate -l $((150 * 1024 * 1024)) $FILE
log_mustnot fallocate -l $((110 * 1024 * 1024)) $FILE
log_must fallocate -l $((90 * 1024 * 1024)) $FILE

log_pass "Ensure sparse files can be preallocated"
