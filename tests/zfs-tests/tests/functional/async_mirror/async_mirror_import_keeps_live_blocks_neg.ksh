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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A writable import of a pool whose lagging child is behind does not
#	write over a block on the lagging child that is live at the txg of
#	the lagging child, also when the pool has freed that block.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write some files and file a, and wait until the lagging child has
#	   caught up.  Keep the digest of a.
#	3. Stop the copy.  Remove a, and let its free pass the deferral.
#	4. Write file b, and export the pool at once, so that the metadata
#	   of b is in the newest txgs.
#	5. Import the pool and export it.
#	6. Hide the leader, and import the lagging child alone, read-only.
#	7. Verify the digest of a.
#
# The import verifies the metadata of the newest txgs, and can repair a
# block of them.  The repair must not reach the lagging child, where the
# range of the block can belong to an older block.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files fill 4
async_mirror_write /$TESTPOOL/a 96
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
typeset digest=$(xxh128digest /$TESTPOOL/a)

async_mirror_suspend_copy 1
log_must rm /$TESTPOOL/a
sync_pool $TESTPOOL
sync_pool $TESTPOOL
sync_pool $TESTPOOL
async_mirror_write /$TESTPOOL/b 96
log_must zpool export $TESTPOOL

async_mirror_import
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/a)"
log_must zpool export $TESTPOOL

log_pass "A writable import does not overwrite a block that is live on" \
    "the lagging child."
