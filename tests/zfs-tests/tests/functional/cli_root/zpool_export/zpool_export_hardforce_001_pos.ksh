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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# Verify that a healthy pool can be exported using hardforce flag.
#
# STRATEGY:
# 1. Create a pool.
# 2. Write some content to check it later.
# 3. Sync.
# 4. Forcibly export.
# 5. Import it back.
# 6. Verify that the content written before is the same.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "zpool export -F of a healthy pool."
log_onexit cleanup

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2

TESTFILE="/$TESTPOOL/file.dd"
log_must dd if=/dev/urandom of=$TESTFILE \
    oflag=sync bs=1M count=10
log_must zpool sync $TESTPOOL
TESTFILE_CKSUM="$(xxh128digest $TESTFILE)"

log_must zpool export -F $TESTPOOL

log_must zpool import $TESTPOOL
log_must test "$TESTFILE_CKSUM" = "$(xxh128digest $TESTFILE)"

log_pass "zpool export -F of a healthy pool."
