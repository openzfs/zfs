#! /bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib
. $STF_SUITE/tests/functional/nopwrite/nopwrite.shlib

default_setup_noexit "$DISKS"
log_onexit default_cleanup_noexit
BLOCKSIZE=8192

origin="$TESTPOOL/$TESTFS"

log_must zfs set compress=on $origin
log_must zfs set checksum=skein $origin

log_must zfs set recordsize=8k $origin
dd if=/dev/urandom of=$TESTDIR/file_8k bs=1024k count=$MEGS oflag=sync \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."
log_must zfs set recordsize=128k $origin
dd if=/dev/urandom of=$TESTDIR/file_128k bs=1024k count=$MEGS oflag=sync \
    conv=notrunc >/dev/null 2>&1 || log_fail "dd into $TESTDIR/file failed."

zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $origin/clone

#
# Verify that nopwrites work prior to removal
#
log_must zfs set recordsize=8k $origin/clone
dd if=/$TESTDIR/file_8k of=/$TESTDIR/clone/file_8k bs=1024k \
     oflag=sync conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_must verify_nopwrite $origin $origin@a $origin/clone

log_must zfs set recordsize=128k $origin/clone
dd if=/$TESTDIR/file_128k of=/$TESTDIR/clone/file_128k bs=1024k \
     oflag=sync conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_must verify_nopwrite $origin $origin@a $origin/clone

#
# Remove a device before testing nopwrites again
#
log_must zpool remove $TESTPOOL $REMOVEDISK
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK

#
# Normally, we expect nopwrites to avoid allocating new blocks, but
# after a device has been removed the DVAs will get remapped when
# a L0's indirect block is written. This will negate the effects
# of nopwrite and should result in new allocations.
#

#
# Perform a direct zil nopwrite test
#
log_must zfs set recordsize=8k $origin/clone
dd if=/$TESTDIR/file_8k of=/$TESTDIR/clone/file_8k bs=1024k \
     oflag=sync conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone

#
# Perform an indirect zil nopwrite test
#
log_must zfs set recordsize=128k $origin/clone
dd if=/$TESTDIR/file_128k of=/$TESTDIR/clone/file_128k bs=1024k \
     oflag=sync conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
log_mustnot verify_nopwrite $origin $origin@a $origin/clone

log_pass "Remove works with nopwrite."
