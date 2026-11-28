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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#
#
# DESCRIPTION:
#	FS_IOC_FSGETXATTR on a directory whose znode lost its SA handle in a
#	rollback fails with EIO, rather than dereferencing the handle.
#
# STRATEGY:
#	1. Snapshot the file system, then create an inheriting directory.
#	2. Open it, roll back past it while it is open, and get its extended
#	   attributes through the open descriptor.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset snap=$TESTPOOL/$TESTFS@fb012

function cleanup
{
	snapexists $snap && destroy_dataset $snap
	rm -rf $TESTDIR/gonedir
}

log_onexit cleanup
log_assert "FS_IOC_FSGETXATTR on a rolled-back znode fails cleanly"

log_must zfs snapshot $snap
log_must mkdir $TESTDIR/gonedir
log_must extsize -s 8192 $TESTDIR/gonedir
verify_ext $TESTDIR/gonedir extszinherit 8192

out=$(extsize -x "zfs rollback $snap" $TESTDIR/gonedir)
log_note "after rollback: $out"
[[ "$out" == "EIO" ]] || log_fail "extsize -x after rollback: $out"
[[ -e $TESTDIR/gonedir ]] && log_fail "rollback left gonedir"

log_pass "FS_IOC_FSGETXATTR on a rolled-back znode fails cleanly"
