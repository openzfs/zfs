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
# Copyright (c) 2023 George Amanakis. All rights reserved.
#

#
# DESCRIPTION:
# Verify reporting errors when deleting corrupted files after scrub
#
# STRATEGY:
# 1. Create a pool, and a file
# 2. Corrupt the file
# 3. Create snapshots and clones like:
# 	fs->snap1->clone1->snap2->clone2->...
# 4. Read the original file and immediately delete it
# 5. Delete the file in clone2
# 6. Snapshot clone2->snapxx and clone into snapxx->clonexx
# 7. Verify we report errors in the pool in 'zpool status -v'
# 8. Promote clone1
# 9. Verify we report errors in the pool in 'zpool status -v'
# 10. Delete the corrupted file and origin snapshots.
# 11. Verify we do not report data errors anymore, without requiring
# 	a scrub.

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_assert "Verify reporting errors when deleting corrupted files after scrub"
log_onexit cleanup

typeset file="/$TESTPOOL2/$TESTFS1/$TESTFILE0"

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f $TESTPOOL2 $TESTDIR/vdev_a
log_must zfs create -o primarycache=none $TESTPOOL2/$TESTFS1
log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
corrupt_blocks_at_level $file 0

lastfs="$(zfs list -r $TESTPOOL2 | tail -1 | awk '{print $1}')"
for i in {1..3}; do
	log_must zfs snap $lastfs@snap$i
	log_must zfs clone $lastfs@snap$i $TESTPOOL2/clone$i
	lastfs="$(zfs list -r $TESTPOOL2/clone$i | tail -1 | awk '{print $1}')"
done

log_must zpool scrub -w $TESTPOOL2
log_must rm $file /$TESTPOOL2/clone2/$TESTFILE0
log_must zfs snap $TESTPOOL2/clone2@snapxx
log_must zfs clone $TESTPOOL2/clone2@snapxx $TESTPOOL2/clonexx
log_must zpool status -v $TESTPOOL2

log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v | grep '$TESTPOOL2/$TESTFS1@snap1:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1@snap2:/$TESTFILE0'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clone2/$TESTFILE0'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clonexx/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2@snap3:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone3/$TESTFILE0'"

log_must zfs promote $TESTPOOL2/clone1
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1@snap1:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone1@snap2:/$TESTFILE0'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clone2/$TESTFILE0'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clonexx/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone2@snap3:/$TESTFILE0'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone3/$TESTFILE0'"

log_must rm /$TESTPOOL2/clone1/$TESTFILE0
log_must zfs destroy -R $TESTPOOL2/clone1@snap1
log_must zfs destroy -R $TESTPOOL2/clone1@snap2
log_must zfs list -r $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_must zpool sync
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"No known data errors\""

log_pass "Verify reporting errors when deleting corrupted files after scrub"
