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
# properly condensed.

# STRATEGY
# 1. Create a pool with disk1 and create a filesystem, snapshot
# and clone. Create two files for the first livelist entry and
# pause condensing.
# 2. Add disk2 to the pool and then remove disk1, triggering a
# remap of the blkptrs tracked in the livelist.
# 3. Overwrite the first file several times to trigger a condense,
# overwrite the second file once and resume condensing, now with
# extra blkptrs added during the remap
# 4. Check that the test added new ALLOC blkptrs mid-condense using
# a variable set in that code path

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

function cleanup
{
	poolexists $TESTPOOL2 && zpool destroy $TESTPOOL2
	# reset livelist max size
	set_tunable64 zfs_livelist_max_entries $ORIGINAL_MAX
	[[ -f $VIRTUAL_DISK1 ]] && log_must rm $VIRTUAL_DISK1
	[[ -f $VIRTUAL_DISK2 ]] && lot_must rm $VIRTUAL_DISK2
}

log_onexit cleanup

ORIGINAL_MAX=$(get_tunable zfs_livelist_max_entries)
set_tunable64 zfs_livelist_max_entries 0x14

VIRTUAL_DISK1=/var/tmp/disk1
VIRTUAL_DISK2=/var/tmp/disk2
log_must mkfile $(($MINVDEVSIZE * 8)) $VIRTUAL_DISK1
log_must mkfile $(($MINVDEVSIZE * 16)) $VIRTUAL_DISK2

log_must zpool create $TESTPOOL2 $VIRTUAL_DISK1
log_must poolexists $TESTPOOL2

log_must zfs create $TESTPOOL2/$TESTFS
log_must mkfile 100m /$TESTPOOL2/$TESTFS/atestfile
log_must zfs snapshot $TESTPOOL2/$TESTFS@snap

log_must zfs clone $TESTPOOL2/$TESTFS@snap $TESTPOOL2/$TESTCLONE

# Create inital files and pause condense zthr on next execution
log_must mkfile 10m /$TESTPOOL2/$TESTCLONE/A
log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/B
log_must zpool sync $TESTPOOL2
set_tunable32 zfs_livelist_condense_sync_pause 1

# Add a new dev and remove the old one
log_must zpool add $TESTPOOL2 $VIRTUAL_DISK2
log_must zpool remove $TESTPOOL2 $VIRTUAL_DISK1
wait_for_removal $TESTPOOL2

set_tunable32 zfs_livelist_condense_new_alloc 0
# Trigger a condense
log_must mkfile 10m /$TESTPOOL2/$TESTCLONE/A
log_must zpool sync $TESTPOOL2
log_must mkfile 10m /$TESTPOOL2/$TESTCLONE/A
log_must zpool sync $TESTPOOL2
# Write remapped blkptrs which will modify the livelist mid-condense
log_must mkfile 1m /$TESTPOOL2/$TESTCLONE/B

# Resume condense thr
set_tunable32 zfs_livelist_condense_sync_pause 0
log_must zpool sync $TESTPOOL2
# Check that we've added new ALLOC blkptrs during the condense
[[ "0" < "$(get_tunable zfs_livelist_condense_new_alloc)" ]] || \
    log_fail "removal/condense test failed"

log_must zfs destroy $TESTPOOL2/$TESTCLONE
log_pass "Clone with the livelist feature and remapped blocks," \
	"can be condensed."
