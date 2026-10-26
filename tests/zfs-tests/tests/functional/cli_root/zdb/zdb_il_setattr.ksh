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
# bits the kernel on this platform logs it with, and prints the mode,
# uid, gid, atime, mtime and, on FreeBSD, size it sets.
#
# Strategy:
# 1. Create a pool with sync=always, a file in it, and freeze the pool.
# 2. Truncate the file, change its mode, its owner and group, then its
#    atime and mtime.
# 3. Verify zdb -iii prints the new mode, uid, gid, atime and mtime,
#    and the new size where the platform logs it.
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
# 1234567890 and 1234567891 seconds after the epoch
log_must env TZ=UTC touch -a -t 200902132331.30 $TESTDIR/file
log_must env TZ=UTC touch -m -t 200902132331.31 $TESTDIR/file

log_must zfs unmount $TESTPOOL
log_must eval "zdb -iii $TESTPOOL > $OUT"

log_must grep -q "TX_SETATTR" $OUT
# Linux logs the file type bits with the mode, FreeBSD does not.
log_must grep -qE "^[[:space:]]*AT_MODE[[:space:]]+(100)?640\$" $OUT
log_must grep -qE "^[[:space:]]*AT_UID[[:space:]]+1234\$" $OUT
log_must grep -qE "^[[:space:]]*AT_GID[[:space:]]+5678\$" $OUT
log_must grep -qE "^[[:space:]]*AT_ATIME[[:space:]]+1234567890\.0+ " $OUT
log_must grep -qE "^[[:space:]]*AT_MTIME[[:space:]]+1234567891\.0+ " $OUT
# zfs_setattr() logs a new size in TX_SETATTR on FreeBSD, but in
# TX_TRUNCATE on Linux.
if is_freebsd; then
	log_must grep -qE "^[[:space:]]*AT_SIZE[[:space:]]+123456\$" $OUT
else
	log_must grep -qE \
	    "^[[:space:]]*foid [0-9]+, offset 0x1e240, length 0x0\$" $OUT
fi

log_pass "zdb -iii prints the attributes a TX_SETATTR record sets"
