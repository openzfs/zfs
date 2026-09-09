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
# Synced data remains readable after converting a pool to readonly
#
# STRATEGY:
# 1. Create a pool with sync=always and write a file
# 2. Remount readonly and convert the pool
# 3. Verify the synced data is still readable
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1/$TESTFS && \
	    ismounted $TESTPOOL1/$TESTFS && \
	    log_must zfs unmount -f $TESTPOOL1/$TESTFS
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV
}

log_assert "Synced data remains readable after converting a pool to readonly"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_004.$$.dat"
TESTFILE="readonly-sync-file"
CONTENT="zpool_set_readonly_004"

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create -o sync=always $TESTPOOL1/$TESTFS
MNTPFS="$(get_prop mountpoint $TESTPOOL1/$TESTFS)"

log_must eval "echo $CONTENT > $MNTPFS/$TESTFILE"
log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS
log_must zpool set readonly=on $TESTPOOL1

log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"
log_must test "$(cat $MNTPFS/$TESTFILE)" == "$CONTENT"

log_pass "Synced data remains readable after converting a pool to readonly"
