#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2015, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

default_setup_noexit "$DISKS"
log_onexit default_cleanup_noexit

log_must dd if=/dev/zero of=$TESTDIR/file bs=1024k count=300

log_must zfs snapshot $TESTPOOL/$TESTFS@snap-pre1
log_must dd if=/dev/zero of=$TESTDIR/file bs=1024k count=100 \
    conv=notrunc seek=100

log_must zfs snapshot $TESTPOOL/$TESTFS@snap-pre2
log_must dd if=/dev/zero of=$TESTDIR/file bs=1024k count=100 \
    conv=notrunc seek=200

if is_linux || is_freebsd; then
	log_must attempt_during_removal $TESTPOOL $REMOVEDISK zdb -cd $TESTPOOL
else
	log_must attempt_during_removal $TESTPOOL $REMOVEDISK
fi
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK
log_must zdb -cd $TESTPOOL

#
# Touch one block under each L1 indirect block, so that the other data blocks
# will be remapped to their concrete locations.  These parameters assume
# recordsize=128K, indirect block size of 128K (1024 block pointers per
# indirect block), and file size of less than 3*1024 blocks (384MB).
#
log_must stride_dd -i /dev/urandom -o $TESTDIR/file -b 131072 -c 3 -s 1024

log_must zdb -cd $TESTPOOL

log_must zfs snapshot $TESTPOOL/$TESTFS@snap-post3
log_must zdb -cd $TESTPOOL

log_must zfs snapshot $TESTPOOL/$TESTFS@snap-post4
log_must zdb -cd $TESTPOOL

#
# Test case where block is moved from remap deadlist: blocks born before
# snap-pre2 will be obsoleted.
#
log_must zfs destroy $TESTPOOL/$TESTFS@snap-pre2
log_must zdb -cd $TESTPOOL

#
# Test case where we merge remap deadlists: blocks before snap-pre1 will
# need to go on snap-post4's deadlist.
#
log_must zfs destroy $TESTPOOL/$TESTFS@snap-post3
log_must zdb -cd $TESTPOOL

log_must zfs destroy $TESTPOOL/$TESTFS@snap-post4

#
# Test rollback.
#
log_must zfs rollback $TESTPOOL/$TESTFS@snap-pre1
log_must zfs destroy $TESTPOOL/$TESTFS@snap-pre1

log_pass "Remove and remap works with snapshots and deadlists."
