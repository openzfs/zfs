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
# Verify that duplicate writes to a clone are accounted as new data if the
# prerequisites for nopwrite are not met.
#
# Scenarios:
# 1. The file in the origin ds is written without compression or sha256.
# 2. The file in the origin ds is written before sha256 checksum is turned on.
# 3. The clone does not have compression.
# 4. The clone does not have the appropriate checksum.
#

verify_runnable "global"
origin="$TESTPOOL/$TESTFS"
log_onexit cleanup

function cleanup
{
	datasetexists $origin && destroy_dataset $origin -R
	log_must zfs create -o mountpoint=$TESTDIR $origin
}

log_assert "nopwrite isn't enabled without the prerequisites"

# Data written into origin fs without compression or sha256
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd of $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone -o compress=on $origin@a $origin/clone
log_must zfs set checksum=sha256 $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone
zfs destroy -R $origin@a || log_fail "zfs destroy failed"
log_must rm -f $TESTDIR/file

# Data written to origin fs before checksum enabled
log_must zfs set compress=on $origin
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
log_must zfs set checksum=sha256 $origin
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone
zfs destroy -R $origin@a || log_fail "zfs destroy failed"
log_must rm -f $TESTDIR/file

# Clone with compression=off
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone -o compress=off $origin@a $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone
zfs destroy -R $origin@a || log_fail "zfs destroy failed"
log_must rm -f $TESTDIR/file

# Clone with fletcher4, rather than sha256
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone -o checksum=fletcher4 $origin@a $origin/clone
dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone

log_pass "nopwrite isn't enabled without the prerequisites"
