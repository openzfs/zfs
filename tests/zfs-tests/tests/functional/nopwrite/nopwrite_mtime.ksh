#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/nopwrite/nopwrite.shlib

#
# Description:
# Verify that nopwrite still updates file metadata correctly
#
# Strategy:
# 1. Create a clone with nopwrite enabled.
# 2. Write to the file in that clone and verify the mtime and ctime change,
# but the atime does not.
#

verify_runnable "global"
origin="$TESTPOOL/$TESTFS"
log_onexit cleanup

function cleanup
{
	datasetexists $origin && destroy_dataset $origin -R
	log_must zfs create -o mountpoint=$TESTDIR $origin
}

log_assert "nopwrite updates file metadata correctly"

log_must zfs set compress=on $origin
log_must zfs set checksum=sha256 $origin
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $origin/clone

if is_linux; then
	o_atime=$(stat -c %X $TESTDIR/clone/file)
	o_ctime=$(stat -c %Z $TESTDIR/clone/file)
	o_mtime=$(stat -c %Y $TESTDIR/clone/file)
elif is_freebsd; then
	o_atime=$(stat -f "%a" $TESTDIR/clone/file)
	o_ctime=$(stat -f "%c" $TESTDIR/clone/file)
	o_mtime=$(stat -f "%m" $TESTDIR/clone/file)
else
	o_atime=$(ls -E% all $TESTDIR/clone/file | awk '/atime/ {print $4}')
	o_ctime=$(ls -E% all $TESTDIR/clone/file | awk '/ctime/ {print $4}')
	o_mtime=$(ls -E% all $TESTDIR/clone/file | awk '/mtime/ {print $4}')
fi

sleep 1
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
sleep 1

if is_linux; then
	atime=$(stat -c %X $TESTDIR/clone/file)
	ctime=$(stat -c %Z $TESTDIR/clone/file)
	mtime=$(stat -c %Y $TESTDIR/clone/file)
elif is_freebsd; then
	atime=$(stat -f "%a" $TESTDIR/clone/file)
	ctime=$(stat -f "%c" $TESTDIR/clone/file)
	mtime=$(stat -f "%m" $TESTDIR/clone/file)
else
	atime=$(ls -E% all $TESTDIR/clone/file | awk '/atime/ {print $4}')
	ctime=$(ls -E% all $TESTDIR/clone/file | awk '/ctime/ {print $4}')
	mtime=$(ls -E% all $TESTDIR/clone/file | awk '/mtime/ {print $4}')
fi

[[ $o_atime = $atime ]] || log_fail "atime changed: $o_atime $atime"
[[ $o_ctime = $ctime ]] && log_fail "ctime unchanged: $o_ctime $ctime"
[[ $o_mtime = $mtime ]] && log_fail "mtime unchanged: $o_mtime $mtime"

log_must verify_nopwrite $origin $origin@a $origin/clone

log_pass "nopwrite updates file metadata correctly"
