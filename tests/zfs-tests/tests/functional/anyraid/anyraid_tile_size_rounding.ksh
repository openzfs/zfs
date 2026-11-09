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
# The tile size of a new AnyRAID vdev is rounded up to a power of two,
# and every disk must hold a tile of the rounded size: creating the vdev
# with a smaller disk fails and lists that disk.
#
# STRATEGY:
# 1. Create three files with 105.5 MiB of room for tiles each.
# 2. With a minimum tile size of 100 MiB, rounded up to 128 MiB, verify
#    that creating an anymirror1 pool on them fails, lists the three
#    files and reports the size of the AnyRAID maps plus a 128 MiB tile
#    as the minimum.
# 3. With a disk shift of 0, which makes the tile the smallest disk
#    rounded up to 128 MiB, verify the same.
# 4. Grow two of the files to hold two 128 MiB tiles each, and verify
#    that with a minimum tile size of 100 MiB the creation fails and
#    lists only the third file.
# 5. Grow the third file too, and verify that the pool is created with
#    three mirrored 128 MiB tiles.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/anyraid_tile_size_rounding
OUT=$TEST_BASE_DIR/anyraid_tile_size_rounding.out
MIB=$((1024 * 1024))
SHIFT=$(get_tunable ANYRAID_DISK_SHIFT)

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DIR $OUT
	set_tunable32 ANYRAID_DISK_SHIFT $SHIFT
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

#
# Each file needs room for the 260 MiB of AnyRAID maps and a 128 MiB
# tile.  $1 lists the numbers of the files that must be reported as too
# small.
#
function create_fails
{
	typeset expected=$1
	typeset listed

	log_mustnot eval "zpool create -f $TESTPOOL anymirror1" \
	    "$DIR/vdev.{0,1,2} > $OUT 2>&1"
	cat $OUT
	log_must grep -q "min device size $(((260 + 128) * MIB))" $OUT
	listed=$(grep -o '/vdev\.[0-9]$' $OUT | cut -d. -f2 | xargs)
	[[ "$listed" == "$expected" ]] || \
	    log_fail "Reported as too small: '$listed', expected '$expected'"
	log_mustnot poolexists $TESTPOOL
}

log_onexit cleanup

log_assert "AnyRAID disks must hold a tile of the rounded tile size"

#
# Past the 4.5 MiB of labels and the 260 MiB of AnyRAID maps, a 370 MiB
# file has 105.5 MiB of room for tiles, and a 600 MiB file 335.5 MiB.
#
log_must mkdir -p $DIR
log_must truncate -s 370M $DIR/vdev.{0,1,2}

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE $((100 * MIB))
create_fails "0 1 2"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE $((64 * MIB))
log_must set_tunable32 ANYRAID_DISK_SHIFT 0
create_fails "0 1 2"
log_must set_tunable32 ANYRAID_DISK_SHIFT $SHIFT

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE $((100 * MIB))
log_must truncate -s 600M $DIR/vdev.{1,2}
create_fails "0"

log_must truncate -s 600M $DIR/vdev.0
log_must zpool create -f $TESTPOOL anymirror1 $DIR/vdev.{0,1,2}
size=$(zpool get -Hp -o value size $TESTPOOL)
[[ $size -eq $((3 * 128 * MIB)) ]] || \
	log_fail "Unexpected size of the anymirror1 pool: $size"

log_pass "AnyRAID disks must hold a tile of the rounded tile size"
