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
