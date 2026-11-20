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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Creating an AnyRAID vdev fails cleanly when one of its children fails
# to open after its size is known, as when the probe of the device
# fails.
#
# STRATEGY:
# 1. Put one of three sparse files on a full tmpfs, where the probe of
#    the device cannot write the blocks of its labels it rewrites.
# 2. Verify that creating an anymirror1 pool on the three files fails
#    with a device reported as not opened, and that no pool is left.
# 3. Move that file out of the tmpfs and verify that the pool is then
#    created.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/anyraid_create_child_fails
MNT=$TEST_BASE_DIR/anyraid_create_child_fails.mnt
OUT=$TEST_BASE_DIR/anyraid_create_child_fails.out
TMO=120

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	umount $MNT > /dev/null 2>&1
	rm -rf $DIR $MNT $OUT
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

log_onexit cleanup

log_assert "Creating AnyRAID fails cleanly when a child fails to open"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864
log_must mkdir -p $DIR $MNT
log_must mount -t tmpfs -o size=16m tmpfs $MNT
log_must truncate -s 768M $MNT/vdev.0 $DIR/vdev.{1,2}
dd if=/dev/zero of=$MNT/fill bs=1M count=32 2>/dev/null

log_mustnot eval "timeout $TMO zpool create -f $TESTPOOL anymirror1" \
    "$MNT/vdev.0 $DIR/vdev.{1,2} > $OUT 2>&1"
cat $OUT
log_must grep -q "one or more devices could not be opened" $OUT
log_mustnot poolexists $TESTPOOL

log_must umount $MNT
log_must truncate -s 768M $DIR/vdev.0
log_must zpool create -f $TESTPOOL anymirror1 $DIR/vdev.{0,1,2}
log_must check_state $TESTPOOL "" "online"

log_pass "Creating AnyRAID fails cleanly when a child fails to open"
