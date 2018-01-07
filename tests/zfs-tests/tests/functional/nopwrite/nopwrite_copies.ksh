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
# Verify that nopwrite is not enabled if the copies property changes
#
# Strategy:
# 1. Create a clone with copies set higher than the origin fs
# 2. Verify that nopwrite is in use.
# 3. Repeat with the number of copies decreased.
#

verify_runnable "global"
origin="$TESTPOOL/$TESTFS"
log_onexit cleanup

function cleanup
{
	datasetexists $origin && log_must zfs destroy -R $origin
	log_must zfs create -o mountpoint=$TESTDIR $origin
}

log_assert "nopwrite requires copies property to remain constant"

# Verify nopwrite is disabled with increased redundancy
log_must zfs set compress=on $origin
log_must zfs set checksum=sha256 $origin
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $origin/clone
zfs set copies=3 $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone

# Verify nopwrite is disabled with decreased redundancy
zfs destroy -R $origin || log_fail "Couldn't destroy $origin"
zfs create -o mountpoint=$TESTDIR $origin || \
    log_fail "Couldn't recreate $origin"
log_must zfs set compress=on $origin
log_must zfs set copies=3 $origin
log_must zfs set checksum=sha256 $origin
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $origin/clone
zfs set copies=1 $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone

log_pass "nopwrite requires copies property to remain constant"
