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

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
#	An AnyRAID mirror written while its largest child is missing still
#	imports once the child is back, and keeps its data.
#
# STRATEGY:
#	1. Create an anymirror1 of children of 1, 1 and 2 GiB (11, 11 and
#	   27 tiles of 64 MiB) and write some data.
#	2. Export, move the largest child away and import the pool degraded.
#	3. Write until a new tile is mapped, then export.
#	4. Move the child back and import the pool.
#	5. Wait for the resilver, check the data and scrub.
#
# The pool's size is 22 tiles: each tile takes the largest child and a
# small one.  A tile mapped on the two small children leaves room for 21,
# and the pool then fails to import, with or without the largest child.
#
# The files are in a directory of their own: "zpool import -d" tries every
# pool it finds, and other tests leave larger files of destroyed pools in
# $TESTDIR/sparse_files.
#

verify_runnable "global"

DEVDIR=$TESTDIR/anyraid_missing_write_import
AWAYDIR=$TESTDIR/anyraid_missing_write_import.away

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DEVDIR $AWAYDIR
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

function tiles # property vdev
{
	zpool get -Hp -o value $1 $TESTPOOL $2
}

function checksums
{
	typeset file

	for file in "/$TESTPOOL"/*; do
		echo "$file $(xxh128digest $file)"
	done
}

log_onexit cleanup

log_assert "AnyRAID mirror written with its largest child missing imports" \
    "once the child is back"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_must mkdir -p $DEVDIR $AWAYDIR
log_must truncate -s 1G $DEVDIR/disk0 $DEVDIR/disk1
log_must truncate -s 2G $DEVDIR/disk2
log_must zpool create -f $TESTPOOL anymirror1 \
    $DEVDIR/disk0 $DEVDIR/disk1 $DEVDIR/disk2

typeset -i i=0
for cap in 11 11 27; do
	(( $(tiles anyraid_tile_capacity $DEVDIR/disk$i) == cap )) || \
	    log_fail "disk$i does not have $cap tiles"
	(( i += 1 ))
done

log_must file_write -o create -f /$TESTPOOL/before -b 1048576 -c 64 -d R
log_must zpool export $TESTPOOL
log_must mv $DEVDIR/disk2 $AWAYDIR/
log_must zpool import -d $DEVDIR $TESTPOOL
log_must check_state $TESTPOOL "" "degraded"

# 64 MiB per file; the pool holds 1408 MiB
typeset -i before
typeset -i after
before=$(tiles anyraid_tile_count anymirror1-0)
after=$before
i=0
while (( after == before )); do
	(( i < 10 )) || log_fail "no new tile after $i files of 64 MiB"
	log_must file_write -o create -f /$TESTPOOL/file.$i -b 1048576 \
	    -c 64 -d R
	log_must zpool sync $TESTPOOL
	(( i += 1 ))
	after=$(tiles anyraid_tile_count anymirror1-0)
done
log_note "Tiles: $before, then $after after $i files;" \
    "disk0 $(tiles anyraid_tile_count $DEVDIR/disk0)," \
    "disk1 $(tiles anyraid_tile_count $DEVDIR/disk1)"
sums=$(checksums)

log_must zpool export $TESTPOOL
log_must mv $AWAYDIR/disk2 $DEVDIR/
log_must zpool import -d $DEVDIR $TESTPOOL

log_must wait_resilvered $TESTPOOL 300
log_must is_pool_resilvered $TESTPOOL
log_must check_state $TESTPOOL "" "online"
if [[ $(checksums) != "$sums" ]]; then
	log_note "Before: $sums"
	log_note "After: $(checksums)"
	log_fail "The data changed"
fi

log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL 300
log_must is_pool_scrubbed $TESTPOOL
log_must zpool status -v $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "AnyRAID mirror written with its largest child missing imports" \
    "once the child is back"
