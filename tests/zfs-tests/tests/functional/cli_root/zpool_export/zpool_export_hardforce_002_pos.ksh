#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# Verify that a healthy pool without mountpoints can be exported using
# hardforce flag.
#
# STRATEGY:
# 1. Create a pool.
# 2. Write some content to check it later.
# 3. Sync.
# 4. Unmount the fs.
# 5. Forcibly export.
# 6. Import it back.
# 7. Verify that the content written before is the same.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "zpool export -F of a healthy pool without mountpoints."
log_onexit cleanup

log_must create_pool $TESTPOOL raidz $FEDISK0 $FEDISK1 $FEDISK2

TESTFILE="/$TESTPOOL/file.dd"
log_must dd if=/dev/urandom of=$TESTFILE \
    oflag=sync bs=1M count=10
log_must zpool sync $TESTPOOL
TESTFILE_CKSUM="$(xxh128digest $TESTFILE)"

log_must zfs unmount /$TESTPOOL

log_must zpool export -F $TESTPOOL

log_must zpool import $TESTPOOL
log_must test "$TESTFILE_CKSUM" = "$(xxh128digest $TESTFILE)"

log_pass "zpool export -F of a healthy pool."
