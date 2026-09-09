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
# zpool set readonly=on can convert a pool from RW to RO
#
# STRATEGY:
# 1. Create a pool and filesystem, write a test file
# 2. Remount the filesystem readonly
# 3. Set readonly=on on the pool
# 4. Verify the pool is readonly and data is still readable
# 5. Verify writes and remount,rw fail
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1/$TESTFS && \
	    log_must zfs unmount -f $TESTPOOL1/$TESTFS
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV
}

log_assert "zpool set readonly=on can convert a pool from RW to RO"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_001.$$.dat"
TESTFILE="readonly-test-file"
CONTENT="zpool_set_readonly_001"

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create $TESTPOOL1/$TESTFS
MNTPFS="$(get_prop mountpoint $TESTPOOL1/$TESTFS)"

log_must eval "echo $CONTENT > $MNTPFS/$TESTFILE"
log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS

log_must zpool set readonly=on $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_must test "$(cat $MNTPFS/$TESTFILE)" == "$CONTENT"
log_mustnot touch $MNTPFS/$TESTFILE.new
log_mustnot zfs create $TESTPOOL1/${TESTFS}_new

# note: remount,rw does not return a failure, but readonly should stay 'on'
log_must zfs mount -o remount,rw $TESTPOOL1/$TESTFS
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_pass "zpool set readonly=on can convert a pool from RW to RO"
