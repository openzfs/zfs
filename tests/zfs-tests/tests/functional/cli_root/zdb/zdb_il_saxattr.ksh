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
# Copyright (c) 2026 by Matthew Ahrens. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -ivvvvv prints TX_SETSAXATTR values as text: a value with a
# non-printable byte, or any value with -P, is printed escaped, and each
# value ends with a newline.
#
# Strategy:
# 1. Create a pool with xattr=sa and sync=always, and freeze it.
# 2. Set a user xattr whose value ends in a NUL byte, and one whose
#    value is printable.
# 3. Verify zdb -ivvvvv prints the first value escaped and the second
#    as is, each on its own line, and emits no NUL bytes.
# 4. Verify zdb -ivvvvvP prints both values escaped.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/zdb_il_saxattr.vdev
OUT=$TEST_BASE_DIR/zdb_il_saxattr.out

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV $OUT
}

log_onexit cleanup

log_assert "zdb -ivvvvv prints TX_SETSAXATTR values as text"

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -O xattr=sa -O sync=always \
    -O mountpoint=$TESTDIR $TESTPOOL $VDEV

# Ensure a ZIL header exists so records are written after the freeze.
log_must dd if=/dev/zero of=$TESTDIR/sync conv=fsync bs=1 count=1
log_must zpool freeze $TESTPOOL

log_must touch $TESTDIR/file
case "$UNAME" in
FreeBSD)
	log_must eval "printf 'zdbval\\000' | \
	    setextattr -i user zdbname $TESTDIR/file"
	log_must setextattr user zdbplain zdbtext $TESTDIR/file
	;;
*)
	# "zdbval" followed by a NUL byte
	log_must setfattr -n user.zdbname -v 0x7a646276616c00 $TESTDIR/file
	log_must setfattr -n user.zdbplain -v zdbtext $TESTDIR/file
	;;
esac

log_must zfs unmount $TESTPOOL
log_must eval "zdb -ivvvvv $TESTPOOL > $OUT"

log_must grep -q "TX_SETSAXATTR" $OUT
# "zdbval\0" and "zdbtext"
typeset zdbval='\\172\\144\\142\\166\\141\\154\\000'
typeset zdbtext='\\172\\144\\142\\164\\145\\170\\164'
log_must grep -qE "^[[:space:]]*XAT_VALUE[[:space:]]+$zdbval\$" $OUT
log_must grep -qE "^[[:space:]]*XAT_VALUE[[:space:]]+zdbtext\$" $OUT
typeset nuls=$(tr -cd '\000' < $OUT | wc -c)
log_must test "$nuls" -eq 0

log_must eval "zdb -ivvvvvP $TESTPOOL > $OUT"
log_must grep -qE "^[[:space:]]*XAT_VALUE[[:space:]]+$zdbval\$" $OUT
log_must grep -qE "^[[:space:]]*XAT_VALUE[[:space:]]+$zdbtext\$" $OUT

log_pass "zdb -ivvvvv prints TX_SETSAXATTR values as text"
