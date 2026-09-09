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
# readonly set by zpool set does not persist across export/import
#
# STRATEGY:
# 1. Create a pool, write data, remount readonly, and convert the pool
# 2. Export and import the pool without -o readonly=on
# 3. Verify the pool is writable and the data is intact
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

log_assert "readonly set by zpool set does not persist across export/import"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_002.$$.dat"
TESTFILE="readonly-persist-file"
CONTENT="zpool_set_readonly_002"

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create $TESTPOOL1/$TESTFS
MNTPFS="$(get_prop mountpoint $TESTPOOL1/$TESTFS)"

log_must eval "echo $CONTENT > $MNTPFS/$TESTFILE"
log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS
log_must zpool set readonly=on $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_must zfs unmount $TESTPOOL1/$TESTFS
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1

log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "off"
MNTPFS="$(get_prop mountpoint $TESTPOOL1/$TESTFS)"
log_must ismounted $TESTPOOL1/$TESTFS
log_must test "$(cat $MNTPFS/$TESTFILE)" == "$CONTENT"
log_must eval "echo rewritten > $MNTPFS/$TESTFILE"

log_pass "readonly set by zpool set does not persist across export/import"
