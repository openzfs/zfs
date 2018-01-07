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
log_must sync
if [[ -s $TESTDIR/$TESTFILE ]]; then
	log_note "$(ls -l $TESTDIR/$TESTFILE)"
	log_fail "testfile not truncated"
fi

log_pass "Successful truncation while a sync operation is in progress."
