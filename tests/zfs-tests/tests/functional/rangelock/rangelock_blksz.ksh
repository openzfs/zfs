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

. $STF_SUITE/tests/functional/rangelock/rangelock.kshlib

#
# DESCRIPTION:
#	A read of a one-block file whose block size grows returns
#	the file as it was before the growth or as it is after.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, write 200 files
#	   of 4K, each in one 4K block, and sync the pool.
#	2. For each file, three threads read it from offset 0
#	   while another extends it past its first block, which
#	   grows the block size to 128K.  Even files grow by a
#	   write at 4K, odd ones by a truncate to 1M.  The block
#	   size must have grown.
#	3. Repeat with files written just before they are
#	   extended, so their block is still dirty.
#	4. Each read must return the first 4K, and either nothing
#	   past it or all of the extension, consistent with when
#	   it ran.
#	5. Fail on any other read, or if in either pass no file
#	   was read while it was extended.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/blksz

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "Reads racing a block size change see the file before" \
    "or after it"
log_onexit cleanup

log_must zfs create -o recordsize=128k -o atime=off $FS
MNT=$(get_prop mountpoint $FS)
log_must rangelock_race -p -f 200 blksz $MNT
sync_pool $TESTPOOL
rl_run 60 120 -e -f 200 blksz $MNT
rl_run 60 120 -f 200 blksz $MNT

log_pass "Reads racing a block size change see the file before" \
    "or after it"
