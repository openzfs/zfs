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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/truncate/truncate.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Tests file truncation within ZFS while a sync operation is in progress.
#
# STRATEGY:
# 1. Copy a file to ZFS filesystem
# 2. Copy /dev/null to same file on ZFS filesystem
# 3. Execute a sync command
#

verify_runnable "both"

function cleanup
{
	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
	[[ -f $srcfile ]] && rm -f $srcfile
}

log_assert "Ensure zeroed file gets written correctly during a sync operation"

srcfile="$TESTDIR/cosmo.$$"
log_must dd if=/dev/urandom of=$srcfile bs=1024k count=1

log_onexit cleanup
log_must cp $srcfile $TESTDIR/$TESTFILE
log_must cp /dev/null $TESTDIR/$TESTFILE
sync_all_pools
if [[ -s $TESTDIR/$TESTFILE ]]; then
	log_note "$(ls -l $TESTDIR/$TESTFILE)"
	log_fail "testfile not truncated"
fi

log_pass "Successful truncation while a sync operation is in progress."
