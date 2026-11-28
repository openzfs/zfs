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
#	Invalid extent size settings are refused and leave nothing behind.
#
# STRATEGY:
#	1. A file with data can't have its block size changed (EINVAL).
#	2. Sizes that aren't a power of 2 from 512 to 128K are refused.
#	3. FS_XFLAG_EXTSIZE on a directory and FS_XFLAG_EXTSZINHERIT on a
#	   file are refused, as on XFS.
#	4. A read-only dataset refuses a change (EROFS).
#	5. Nothing is set after a refusal.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

function cleanup
{
	zfs set readonly=off $TESTPOOL/$TESTFS
	rm -rf $TESTDIR/full $TESTDIR/empty $TESTDIR/dir
}

log_onexit cleanup
log_assert "Invalid extent size settings are refused"

write_mib $TESTDIR/full
extsize_fails EINVAL -s 4096 $TESTDIR/full
verify_ext $TESTDIR/full none 0

log_must touch $TESTDIR/empty
log_must mkdir $TESTDIR/dir
typeset bad
for bad in 3000 256 1 262144 1048576; do
	extsize_fails EINVAL -s $bad $TESTDIR/empty
	extsize_fails EINVAL -s $bad $TESTDIR/dir
done
extsize_fails EINVAL -f 800 -s 4096 $TESTDIR/dir
extsize_fails EINVAL -f 1000 -s 4096 $TESTDIR/empty
extsize_fails EINVAL -f 1800 -s 4096 $TESTDIR/empty
verify_ext $TESTDIR/empty none 0
verify_ext $TESTDIR/dir none 0

log_must zfs set readonly=on $TESTPOOL/$TESTFS
extsize_fails EROFS -s 4096 $TESTDIR/empty
extsize_fails EROFS -s 4096 $TESTDIR/dir
log_must zfs set readonly=off $TESTPOOL/$TESTFS
verify_ext $TESTDIR/empty none 0

log_pass "Invalid extent size settings are refused"
