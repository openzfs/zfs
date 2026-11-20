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

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Ensure that we can rewind to a checkpoint that holds a claimed
#	but unreplayed ZIL, and that the log is still replayed afterwards.
#
# STRATEGY:
#	1. Create a pool and a file system
#	2. Freeze the pool and write a file synchronously, so that it only
#	   lives in the ZIL
#	3. Export, then import without mounting: the log is claimed but
#	   not replayed
#	4. Take a checkpoint, export, and rewind to it
#	5. Mount the file system and verify that the file was replayed
#

verify_runnable "global"

log_onexit cleanup_test_pool

log_must zpool create $TESTPOOL "$TESTDISK"
log_must zfs create $TESTPOOL/$TESTFS

#
# ZIL records are not created after freezing the pool unless a ZIL
# header already exists.
#
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    conv=fdatasync,fsync bs=1 count=1

log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/payload \
    oflag=sync bs=128k count=8
typeset checksum=$(xxh128digest /$TESTPOOL/$TESTFS/payload)

log_must zpool export $TESTPOOL
log_must zpool import -N $TESTPOOL
sync_pool $TESTPOOL
log_must eval "zdb -iv $TESTPOOL/$TESTFS | grep -q 'claim_txg [1-9]'"

log_must zpool checkpoint $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -N --rewind-to-checkpoint $TESTPOOL

log_must zfs mount $TESTPOOL/$TESTFS
log_must test "$(xxh128digest /$TESTPOOL/$TESTFS/payload)" = "$checksum"

log_pass "Rewind to a checkpoint holding a claimed ZIL."
