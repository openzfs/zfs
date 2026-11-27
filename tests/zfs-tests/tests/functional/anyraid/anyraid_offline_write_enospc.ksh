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
#	An AnyRAID mirror with children offline can be filled: no child gets
#	more tiles than its capacity, and the pool resilvers, scrubs, exports
#	and imports without errors.
#
# STRATEGY:
#	For an anymirror1 of three children with one offline, and an
#	anymirror2 of four children with two offline, each child 12 tiles:
#	1. Write some data, then take the children offline.
#	2. Write until ENOSPC.  The online children cannot hold every new
#	   tile, so some must go to an offline child.
#	3. Bring the children back online, wait for the resilver, scrub.
#	4. Check that no child has more tiles than its capacity, that the
#	   pool is healthy, and that it exports, imports and scrubs cleanly.
#
# Writes, syncs, the export and the import run in the background with a
# time limit, so that a hang fails the test instead of stalling the suite.
#
# The files are in a directory of their own: "zpool import -d" tries every
# pool it finds, and other tests leave larger files of destroyed pools in
# $TESTDIR/sparse_files.
#

verify_runnable "global"

BOUNDED_OUT=$TEST_BASE_DIR/anyraid_offline_write_enospc.out
DEVDIR=$TEST_BASE_DIR/anyraid_offline_write_enospc
STUCK=""
disks=""

function cleanup
{
	if [[ -n $STUCK ]]; then
		log_note "A command did not return; leaving $TESTPOOL as it is"
	else
		poolexists $TESTPOOL && destroy_pool $TESTPOOL
		rm -rf $DEVDIR
	fi
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $BOUNDED_OUT
}

#
# Run a command in the background and wait at most $1 seconds for it.
# Return its exit status, or 124 if it is still running.
#
function run_bounded # seconds command...
{
	typeset -i limit=$1
	typeset -i waited=0
	typeset -i rc
	shift

	"$@" </dev/null >$BOUNDED_OUT 2>&1 &
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if (( waited >= limit )); then
			STUCK=1
			log_note "'$*' still running after $limit seconds"
			return 124
		fi
		sleep 1
		(( waited += 1 ))
	done
	wait $pid
	rc=$?
	cat $BOUNDED_OUT
	return $rc
}

function tiles # property disk
{
	zpool get -Hp -o value $1 $TESTPOOL $2
}

function check_tiles
{
	typeset disk
	typeset -i cap
	typeset -i count

	for disk in $disks; do
		cap=$(tiles anyraid_tile_capacity $disk)
		count=$(tiles anyraid_tile_count $disk)
		log_note "$disk: $count tiles of $cap"
		(( count <= cap )) || log_fail "$disk has $count tiles," \
		    "more than its capacity of $cap"
	done
}

function check_healthy
{
	log_must check_state $TESTPOOL "" "online"
	log_must eval "zpool status -x $TESTPOOL | grep -q 'is healthy'"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
}

#
# Wait until no resilver runs or is about to start: a child brought online
# during a resilver gets a deferred one, started after the first ends.
#
function wait_resilvers
{
	typeset -i n

	for (( n = 0; n < 10; n++ )); do
		log_must run_bounded 300 zpool wait -t resilver $TESTPOOL
		log_must run_bounded 300 zpool sync $TESTPOOL
		log_must run_bounded 300 zpool sync $TESTPOOL
		is_pool_resilvering $TESTPOOL && continue
		zpool status $TESTPOOL | grep -q "awaiting resilver" && continue
		log_must is_pool_resilvered $TESTPOOL
		return
	done
	log_fail "resilvers still running after $n rounds"
}

function scrub_and_wait
{
	log_must zpool scrub $TESTPOOL
	log_must wait_scrubbed $TESTPOOL 300
	log_must is_pool_scrubbed $TESTPOOL
}

#
# Fill an anymirror<nparity> of <children> children of 12 tiles while the
# first <nparity> children are offline.
#
function offline_fill # nparity children
{
	typeset -i nparity=$1
	typeset -i children=$2
	typeset -i cap
	typeset -i before=0
	typeset -i after=0
	typeset -i i=0
	typeset -i rc=0
	typeset offline=""
	typeset disk

	log_note "anymirror$nparity of $children children, $nparity offline"
	log_must mkdir -p $DEVDIR
	disks=""
	for (( i = 0; i < children; i++ )); do
		# 260 MiB of tile maps, the labels and 12 tiles of 64 MiB
		log_must truncate -s 1088M $DEVDIR/disk$i
		disks="$disks $DEVDIR/disk$i"
	done
	log_must zpool create -f $TESTPOOL anymirror$nparity $disks

	for disk in $disks; do
		cap=$(tiles anyraid_tile_capacity $disk)
		(( cap == 12 )) || log_fail "$disk has $cap tiles, expected 12"
	done
	for (( i = 0; i < nparity; i++ )); do
		offline="$offline $DEVDIR/disk$i"
	done

	log_must file_write -o create -f /$TESTPOOL/before -b 1048576 \
	    -c 64 -d R
	log_must run_bounded 300 zpool sync $TESTPOOL
	for disk in $offline; do
		(( before += $(tiles anyraid_tile_count $disk) ))
		log_must zpool offline $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "offline"
	done

	# 64 MiB per file; neither pool holds more than 1152 MiB
	i=0
	while (( i < 40 )); do
		run_bounded 300 file_write -o create -f /$TESTPOOL/fill.$i \
		    -b 1048576 -c 64 -d R
		rc=$?
		(( rc == 124 )) && log_fail "write $i did not return in 300 s"
		(( rc != 0 )) && break
		(( i += 1 ))
	done
	# file_write exits with errno: 28 is ENOSPC on Linux and FreeBSD
	(( rc == 28 )) || log_fail "write $i ended with $rc, not ENOSPC"
	log_note "ENOSPC after $i files of 64 MiB"
	log_must run_bounded 300 zpool sync $TESTPOOL
	check_tiles

	for disk in $offline; do
		(( after += $(tiles anyraid_tile_count $disk) ))
	done
	(( after > before )) || log_fail "the offline children got no new" \
	    "tile ($before before, $after after)"

	for disk in $offline; do
		log_must zpool online $TESTPOOL $disk
	done
	wait_resilvers
	scrub_and_wait
	check_healthy
	check_tiles

	log_must run_bounded 300 zpool export $TESTPOOL
	log_must run_bounded 300 zpool import -d $DEVDIR $TESTPOOL
	check_tiles
	scrub_and_wait
	check_healthy

	log_must destroy_pool $TESTPOOL
	log_must rm -rf $DEVDIR
}

log_onexit cleanup

log_assert "AnyRAID mirror with children offline keeps every child's tiles" \
    "within its capacity"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

offline_fill 1 3
offline_fill 2 4

log_pass "AnyRAID mirror with children offline keeps every child's tiles" \
    "within its capacity"
