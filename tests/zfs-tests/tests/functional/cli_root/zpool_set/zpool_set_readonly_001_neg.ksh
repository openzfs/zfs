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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set readonly=on fails while a filesystem is mounted writable
#
# STRATEGY:
# 1. Create a pool with a filesystem mounted read-write
# 2. Verify zpool set readonly=on fails
# 3. Verify the pool remains writable
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV
}

log_assert "zpool set readonly=on fails while a filesystem is mounted writable"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_001_neg.$$.dat"
TESTFILE="readonly-neg-file"

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create $TESTPOOL1/$TESTFS
MNTPFS="$(get_prop mountpoint $TESTPOOL1/$TESTFS)"

log_mustnot zpool set readonly=on $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "off"
log_must touch $MNTPFS/$TESTFILE

log_pass "zpool set readonly=on fails while a filesystem is mounted writable"
