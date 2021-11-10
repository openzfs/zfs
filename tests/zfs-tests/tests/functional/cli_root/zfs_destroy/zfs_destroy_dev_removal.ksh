#!/bin/ksh -p
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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

# DESCRIPTION
# Verify that livelists tracking remapped blocks can be
# properly destroyed.

# STRATEGY
# 1. Create a pool with disk1 and create a filesystem, snapshot
# and clone. Write several files to the clone.
# 2. Add disk2 to the pool and then remove disk1, triggering a
# remap of the blkptrs tracked in the livelist.
# 3. Delete the clone

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

function cleanup
{
	poolexists $TESTPOOL2 && zpool destroy $TESTPOOL2
	[[ -f $VIRTUAL_DISK1 ]] && log_must rm $VIRTUAL_DISK1
	[[ -f $VIRTUAL_DISK2 ]] && log_must rm $VIRTUAL_DISK2
}

log_onexit cleanup

VIRTUAL_DISK1=$TEST_BASE_DIR/disk1
VIRTUAL_DISK2=$TEST_BASE_DIR/disk2
log_must truncate -s $(($MINVDEVSIZE * 8)) $VIRTUAL_DISK1
log_must truncate -s $(($MINVDEVSIZE * 16)) $VIRTUAL_DISK2

log_must zpool create $TESTPOOL2 $VIRTUAL_DISK1
log_must poolexists $TESTPOOL2

log_must zfs create $TESTPOOL2/$TESTFS
log_must mkfile 25m /$TESTPOOL2/$TESTFS/atestfile
log_must zfs snapshot $TESTPOOL2/$TESTFS@snap

log_must zfs clone $TESTPOOL2/$TESTFS@snap $TESTPOOL2/$TESTCLONE

log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/$TESTFILE0
log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/$TESTFILE1
log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/$TESTFILE2

log_must zpool add $TESTPOOL2 $VIRTUAL_DISK2
log_must zpool remove $TESTPOOL2 $VIRTUAL_DISK1
wait_for_removal $TESTPOOL2

log_must rm /$TESTPOOL2/$TESTCLONE/$TESTFILE0
log_must rm /$TESTPOOL2/$TESTCLONE/$TESTFILE1

log_must zfs destroy $TESTPOOL2/$TESTCLONE

log_pass "Clone with the livelist feature and remapped blocks," \
	"can be destroyed."
