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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The scratch step of an expansion that adds several disks writes the
#	start of every disk, old and new, in the new layout.  Here more disks
#	are added than the parity can hide: if the start of two or more new
#	disks were left stale, a raidz1 row there could not be reconstructed,
#	and if one were, a scrub would repair it.
#
# STRATEGY:
#	For each layout (parity, old width, disks added), with more disks
#	added than parity + 1:
#	1. Create the raidz pool, fill it, record a checksum of every file.
#	2. Attach the new disks with raidz_expand_max_reflow_bytes at 1.
#	   The scratch step does not count as copied, so the reflow stops
#	   right after it, once the first copy past the scratch area has
#	   synced.
#	3. Verify the files.  Scrub: no checksum errors, nothing repaired.
#	4. Export, check every block with zdb, import, verify the files.
#	5. Let the copy finish.  Scrub: no checksum errors, nothing repaired.
#	   Verify the files.  Export and check every block with zdb.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_multi_004
typeset -a layouts=("1 3 3" "1 5 4")
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf $devdir

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function record_sums
{
	for f in /$TESTPOOL/fs/*/*; do
		sums[$f]=$(xxh128digest $f)
	done
}

function verify_sums
{
	typeset -i n=0
	for f in "${!sums[@]}"; do
		if [[ "$(xxh128digest $f)" != "${sums[$f]}" ]]; then
			log_fail "checksum of $f changed"
		fi
		(( n = n + 1 ))
	done
	if [[ $n -eq 0 ]]; then
		log_fail "no files were checked"
	fi
}

function check_feature # <state>
{
	typeset state=$(zpool get -H -o value \
	    feature@raidz_expansion_multi $TESTPOOL)
	if [[ "$state" != "$1" ]]; then
		log_fail "raidz_expansion_multi is $state, expected $1"
	fi
}

#
# Nothing is counted as copied until the scratch step is over, so first
# wait for the copied amount to leave 0, then for the reflow to stop.
#
function wait_past_scratch
{
	typeset -i t0=$SECONDS
	typeset -i copied=0

	while :; do
		copied=$(zpool status -jp $TESTPOOL | jq -r \
		    ".pools[\"$TESTPOOL\"].raidz_expand_stats.reflowed // 0")
		(( copied > 0 )) && break
		if (( SECONDS - t0 > 300 )); then
			log_fail "nothing copied past the scratch area in 300s"
		fi
		sleep 1
	done
	wait_raidz_expand_paused $TESTPOOL
}

log_onexit cleanup

# A directory of its own: zdb -e -p would also find the test suite's
# own file vdevs in $TEST_BASE_DIR, which can carry a pool of the same
# name.
log_must mkdir -p $devdir

log_assert "the scratch step writes the start of every new disk"

for layout in "${layouts[@]}"; do
	set -- $layout
	typeset -i parity=$1 width=$2 added=$3
	typeset -i total=$((width + added))
	typeset -a disks=()
	typeset -a newdisks=()

	log_note "raidz$parity of $width, attaching $added disks," \
	    "stopped after the scratch step"

	for i in {1..$total}; do
		device=$devdir/dev-$i
		log_must truncate -s ${dev_size_mb}M $device
		if [[ $i -le $width ]]; then
			disks[${#disks[*]}+1]=$device
		else
			newdisks[${#newdisks[*]}+1]=$device
		fi
	done

	log_must zpool create -f -o cachefile=none \
	    -O primarycache=metadata $TESTPOOL raidz$parity "${disks[@]}"
	log_must zfs create -o recordsize=128k $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 64 1048576 1 R
	record_sums

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 1
	log_must zpool attach $TESTPOOL raidz$parity-0 "${newdisks[@]}"
	wait_past_scratch
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	check_feature active

	verify_sums
	verify_pool $TESTPOOL
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	log_must_busy zpool export $TESTPOOL
	log_must zdb -e -p $devdir -bcc $TESTPOOL
	log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	verify_sums

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool wait -t raidz_expand $TESTPOOL
	check_feature enabled

	is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
	verify_pool $TESTPOOL
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	verify_sums

	log_must_busy zpool export $TESTPOOL
	log_must zdb -e -p $devdir -bcc $TESTPOOL
	log_must rm -f $devdir/dev-*
done

log_pass "the scratch step writes the start of every new disk"
