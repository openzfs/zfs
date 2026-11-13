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
#	A new disk that goes away while an expansion that adds several disks
#	is paused does not lose data.  The disk can be brought back online,
#	or replaced by a fresh disk (replacing a child is allowed while an
#	expansion runs; the reflow waits until the replacement is done), and
#	the expansion then finishes.  With parity p, p new disks can be
#	offline at once and the pool is still readable.
#
# STRATEGY:
#	1. raidz1 of 5 plus 2, paused half way: offline one new disk, write
#	   more data, verify the files, online the disk and let it resilver.
#	2. raidz1 of 3 plus 3, paused right after the scratch step: replace
#	   one new disk with a fresh file and wait for the replacement.
#	3. raidz2 of 6 plus 3, paused half way: offline two new disks at
#	   once, verify the files, online both and let them resilver.
#	Each case then lets the copy finish, scrubs (no checksum errors,
#	nothing repaired) and verifies every file checksum.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -f $TEST_BASE_DIR/dev-*

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
}

function record_sums
{
	for f in /$TESTPOOL/fs*/*/*; do
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

#
# Create a raidz pool of $2 disks with parity $1, fill it, and attach $3
# more disks with the reflow paused: half way when $4 is "half", right
# after the scratch step when it is "scratch".
#
function setup_paused # <parity> <width> <added> <half|scratch>
{
	typeset -i parity=$1 width=$2 added=$3
	typeset -i total=$((width + added))

	for f in "${!sums[@]}"; do
		unset "sums[$f]"
	done
	unset disks newdisks
	for i in {1..$total}; do
		device=$TEST_BASE_DIR/dev-$i
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

	if [[ $4 == "half" ]]; then
		reflow_size=$(get_pool_prop allocated $TESTPOOL)
		log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES \
		    $((reflow_size / 2))
	else
		log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 1
	fi
	log_must zpool attach $TESTPOOL raidz$parity-0 "${newdisks[@]}"
	wait_past_scratch
	log_must check_pool_status $TESTPOOL "expand" "in progress"
}

#
# Let the paused copy finish, then check that nothing was lost or needed
# repair, and destroy the pool.
#
function finish_and_verify
{
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must zpool wait -t raidz_expand $TESTPOOL
	log_must zpool wait -t resilver $TESTPOOL

	is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
	verify_pool $TESTPOOL
	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	verify_sums

	log_must zpool destroy $TESTPOOL
	log_must rm -f $TEST_BASE_DIR/dev-*
}

log_onexit cleanup

log_assert "a new disk can fail during a several-disk raidz expansion"

typeset -a disks newdisks

# 1. One new disk offline, then back online
log_note "raidz1 of 5 plus 2: offline and online a new disk"
setup_paused 1 5 2 half
log_must zpool offline $TESTPOOL ${newdisks[1]}
log_must check_vdev_state $TESTPOOL ${newdisks[1]} "OFFLINE"
log_must check_state $TESTPOOL "" "DEGRADED"
verify_sums
log_must zfs create -o recordsize=128k $TESTPOOL/fs2
log_must fill_fs /$TESTPOOL/fs2 1 16 1048576 1 R
record_sums
log_must zpool online $TESTPOOL ${newdisks[1]}
log_must zpool wait -t resilver $TESTPOOL
log_must check_state $TESTPOOL "" "ONLINE"
verify_sums
finish_and_verify

# 2. One new disk replaced by a fresh one
log_note "raidz1 of 3 plus 3: replace a new disk"
setup_paused 1 3 3 scratch
fresh=$TEST_BASE_DIR/dev-fresh
log_must truncate -s ${dev_size_mb}M $fresh
log_must zpool replace -w $TESTPOOL ${newdisks[2]} $fresh
log_mustnot eval "zpool status $TESTPOOL | grep -q '${newdisks[2]} '"
log_must eval "zpool status $TESTPOOL | grep -q '$fresh '"
log_must check_pool_status $TESTPOOL "expand" "in progress"
verify_sums
finish_and_verify

# 3. Parity-many new disks offline at once
log_note "raidz2 of 6 plus 3: offline two new disks at once"
setup_paused 2 6 3 half
log_must zpool offline $TESTPOOL ${newdisks[1]} ${newdisks[3]}
log_must check_vdev_state $TESTPOOL ${newdisks[1]} "OFFLINE"
log_must check_vdev_state $TESTPOOL ${newdisks[3]} "OFFLINE"
verify_sums
log_must_busy zpool export $TESTPOOL
log_must zpool import -o cachefile=none -d $TEST_BASE_DIR $TESTPOOL
log_must check_state $TESTPOOL "" "DEGRADED"
verify_sums
log_must zpool online $TESTPOOL ${newdisks[1]} ${newdisks[3]}
log_must zpool wait -t resilver $TESTPOOL
log_must check_state $TESTPOOL "" "ONLINE"
finish_and_verify

log_pass "a new disk can fail during a several-disk raidz expansion"
