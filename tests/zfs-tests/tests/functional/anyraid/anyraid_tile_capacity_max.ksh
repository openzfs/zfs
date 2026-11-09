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

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# An AnyRAID child can hold the largest number of tiles that the format
# allows, 2^16, and that capacity survives an import without the child.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 16 MiB tiles on two small sparse
#    files and one large enough for 2^16 tiles, and write a file.
# 2. Export the pool, move the large file away and import the pool
#    read-only, so that nothing is allocated while it is degraded, and
#    check the capacity of that child, now read from the tile map.
# 3. Move the file back, import the pool and verify the file and the
#    capacity, then scrub the pool.
#

verify_runnable "global"

AWAY=$TESTDIR/anyraid_tile_capacity_max.away

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	delete_sparse_files
	rm -f $AWAY
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

function check_capacity # vdev
{
	typeset cap

	cap=$(zpool get -Hp -o value anyraid_tile_capacity $TESTPOOL $1)
	[[ "$cap" == "65536" ]] || log_fail "$1 holds $cap tiles, not 65536"
}

log_onexit cleanup

log_assert "An AnyRAID child can hold 2^16 tiles"

#
# 16 MiB tiles, the smallest allowed, keep the large file at 1 TiB of
# tiles, plus its labels and the 260 MiB of tile maps.
#
log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 16777216
create_sparse_files "small" 2 512M
create_sparse_files "large" 1 1025G
log_must zpool create -f $TESTPOOL anymirror1 $small0 $small1 $large0
check_capacity $large0

log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 16 -d R
cksum=$(xxh128digest /$TESTPOOL/file.bin)
log_must zpool export $TESTPOOL

log_must mv $large0 $AWAY
log_must zpool import -o readonly=on -d $TESTDIR/sparse_files $TESTPOOL
log_must check_state $TESTPOOL "" "degraded"
check_capacity $large0
log_must zpool export $TESTPOOL
log_must mv $AWAY $large0

log_must zpool import -d $TESTDIR/sparse_files $TESTPOOL
log_must check_state $TESTPOOL "" "online"
check_capacity $large0
[[ $(xxh128digest /$TESTPOOL/file.bin) == "$cksum" ]] || \
	log_fail "file.bin changed across the imports"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "An AnyRAID child can hold 2^16 tiles"
