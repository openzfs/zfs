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
#	A free of a block that a removal copied to the mirror is held.  The
#	free takes the path of a remapped block.
#
# STRATEGY:
#	1. Create a pool with a mirror and a second vdev, and write data.
#	2. Remove the second vdev, and wait for the removal to finish.
#	3. Turn the async mirror on.
#	4. Slow the lagging child and remove the files.
#	5. Verify that the freed blocks are held.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# A removal needs free space for the data of the vdev plus the slop
# space, which is at least 128 MB (spa_min_slop).  With 128 MB vdevs the
# pool cannot hold that, and "zpool remove" fails with "out of space".
#
ASYNC_MIRROR_VDEV_SIZE=256m
async_mirror_create_pool $ASYNC_MIRROR_EXTRA
async_mirror_write_files file 8
log_must zpool remove -w $TESTPOOL $ASYNC_MIRROR_EXTRA

async_mirror_enable
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -D 500:1 $TESTPOOL
log_must rm /$TESTPOOL/file.*
sync_pool $TESTPOOL
typeset -i held=$(get_async_mirror_stat held_bytes)
log_must zinject -c all

log_note "held $held bytes"
log_must test $held -ge 2097152

log_pass "A free of a block from a removed vdev is held."
