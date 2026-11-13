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
# Description:
# zdb -iii decodes the attribute mask of a TX_SETATTR record with the
# bits the kernel on this platform logs it with, and prints the size,
# mode, uid and gid it sets.
#
# Strategy:
# 1. Create a pool with sync=always, a file in it, and freeze the pool.
# 2. Truncate the file, change its mode, then its owner and group.
# 3. Verify zdb -iii prints the new size, mode, uid and gid.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/zdb_il_setattr.vdev
OUT=$TEST_BASE_DIR/zdb_il_setattr.out

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV $OUT
}

log_onexit cleanup

log_assert "zdb -iii prints the attributes a TX_SETATTR record sets"

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -O sync=always -O mountpoint=$TESTDIR $TESTPOOL $VDEV

# Ensure a ZIL header exists so records are written after the freeze.
log_must dd if=/dev/zero of=$TESTDIR/sync conv=fsync bs=1 count=1
log_must touch $TESTDIR/file
log_must zpool freeze $TESTPOOL

log_must truncate -s 123456 $TESTDIR/file
log_must chmod 0640 $TESTDIR/file
log_must chown 1234:5678 $TESTDIR/file

log_must zfs unmount $TESTPOOL
log_must eval "zdb -iii $TESTPOOL > $OUT"

log_must grep -q "TX_SETATTR" $OUT
log_must grep -qE "^[[:space:]]*AT_SIZE[[:space:]]+123456\$" $OUT
# Linux logs the file type bits with the mode, FreeBSD does not.
log_must grep -qE "^[[:space:]]*AT_MODE[[:space:]]+(100)?640\$" $OUT
log_must grep -qE "^[[:space:]]*AT_UID[[:space:]]+1234\$" $OUT
log_must grep -qE "^[[:space:]]*AT_GID[[:space:]]+5678\$" $OUT

log_pass "zdb -iii prints the attributes a TX_SETATTR record sets"
