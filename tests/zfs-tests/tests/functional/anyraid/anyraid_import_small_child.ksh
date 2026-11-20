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
# An AnyRAID child that has become too small for the tiles the map
# records for it is reported unavailable on import, and the pool is
# imported degraded, or refused when too many children are short.
#
# STRATEGY:
# 1. Create an anymirror1 pool of three files with 64 MiB tiles, write
#    a file and export the pool.
# 2. Shrink one file below the size of a single tile.
# 3. Verify that "zpool import -d" lists that child as UNAVAIL, and that
#    the pool imports degraded, with its data intact and writable.
# 4. Export the pool, shrink a second file below its tiles.
# 5. Verify that "zpool import -d" lists the pool without hanging and
#    that the pool cannot be imported.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/anyraid_import_small_child
LISTING=$TEST_BASE_DIR/anyraid_import_small_child.out
TMO=120

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DIR $LISTING
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

log_onexit cleanup

log_assert "AnyRAID children too small for their tiles fail on import"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864
log_must mkdir -p $DIR
log_must truncate -s 768M $DIR/vdev.{0,1,2}
log_must zpool create -f $TESTPOOL anymirror1 $DIR/vdev.{0,1,2}

log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 64 -d R
cksum=$(xxh128digest /$TESTPOOL/file.bin)
log_must zpool export $TESTPOOL

#
# Each child holds the 260 MiB of AnyRAID maps and seven 64 MiB tiles.
# At 300 MiB, a child still opens but has room for no tile.
#
log_must truncate -s 300M $DIR/vdev.0

log_must eval "timeout $TMO zpool import -d $DIR > $LISTING"
cat $LISTING
log_must grep -Eq "vdev\.0 +UNAVAIL" $LISTING

log_must timeout $TMO zpool import -d $DIR $TESTPOOL
log_must zpool status $TESTPOOL
log_must check_state $TESTPOOL "" "degraded"
log_must check_state $TESTPOOL $DIR/vdev.0 "unavail"
log_must check_state $TESTPOOL $DIR/vdev.1 "online"
[[ $(xxh128digest /$TESTPOOL/file.bin) == "$cksum" ]] || \
	log_fail "file.bin changed across the import"
log_must file_write -o create -f /$TESTPOOL/file2.bin -b 1048576 -c 16 -d R
log_must zpool sync $TESTPOOL
log_must zpool export $TESTPOOL

#
# At 400 MiB, a child has room for two of its seven tiles.  With two of
# the three children unavailable, anymirror1 cannot be opened.  The
# import above recorded vdev.0 as not present, so it is listed by guid.
#
log_must truncate -s 400M $DIR/vdev.1

log_must eval "timeout $TMO zpool import -d $DIR > $LISTING"
cat $LISTING
log_must grep -Eq "vdev\.1 +UNAVAIL" $LISTING
log_must grep -Eq "anymirror1-0 +UNAVAIL +insufficient replicas" $LISTING
log_mustnot timeout $TMO zpool import -d $DIR $TESTPOOL
log_mustnot poolexists $TESTPOOL

log_pass "AnyRAID children too small for their tiles fail on import"
