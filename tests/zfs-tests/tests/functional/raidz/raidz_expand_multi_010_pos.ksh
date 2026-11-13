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
#	The disks added by a raidz expansion count like the old disks of the
#	raidz vdev from the attach on, even while less than half of the
#	allocated space has been copied, and they cannot be detached.  As for
#	any raidz vdev, if more of its disks than its parity are missing, old
#	or new, the vdev cannot be opened and the pool cannot be imported, not
#	even read-only.  When more disks than the parity are added at once,
#	parity + 1 of the new disks alone are enough.  With parity of them
#	missing, the pool imports degraded, read-only and read-write, and
#	every file reads back.
#
# STRATEGY:
#	For each layout (parity, old width, disks added), with more disks
#	added than parity:
#	1. Create the raidz pool, fill it, record a checksum of every file.
#	2. Attach the new disks with raidz_expand_max_reflow_bytes at 1, so
#	   that the reflow stops soon after the scratch step.  Check that,
#	   counting the scratch area, less than half of the allocated space
#	   has been copied.  Detaching a new disk is refused.
#	3. Offline parity of the new disks: allowed.  Offlining one more new
#	   disk is refused, so an administrator cannot reach the state below.
#	   Online the disks and let them resilver.
#	4. Export.  The pool is listed ONLINE.
#	5. Move parity + 1 of the new disks away.  The pool and the raidz
#	   vdev are listed UNAVAIL, and the pool cannot be imported, neither
#	   read-write nor read-only.
#	6. raidz1 only: one old and one new disk away give the same refusal.
#	7. Move one disk back, so that parity of the new disks are missing.
#	   The pool is listed DEGRADED.  As the control for the read-only
#	   refusal in step 5, it imports read-only, DEGRADED with the
#	   expansion still in progress, and every file checksum verifies;
#	   export it.  It imports read-write the same way.
#	8. Move the disks back, online them and let them resilver.  Let the
#	   copy finish.  Scrub: no checksum errors, nothing repaired.  Verify
#	   every file checksum.
#

verify_runnable "global"

typeset -r dev_size_mb=256
typeset -r devdir=$TEST_BASE_DIR/raidz_expand_multi_010
typeset -r away=$TEST_BASE_DIR/raidz_expand_multi_010.away
typeset -a layouts=("1 3 3" "2 5 3" "3 5 4")
typeset -A sums

function cleanup
{
	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	log_must rm -rf $devdir $away

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
# zpool online only asks for a resilver.  An async task starts it in a
# later txg, and zpool wait -t resilver returns at once until then, so
# sync twice before waiting.
#
function online_and_resilver # <device> ...
{
	log_must zpool online $TESTPOOL "$@"
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
	log_must zpool wait -t resilver $TESTPOOL
}

#
# The disk files are opened by the path in their labels, so a disk is
# made to go missing by moving its file out of $devdir.
#
function move_away # <device> ...
{
	for d in "$@"; do
		log_must mv $d $away/
	done
}

function move_back # <device> ...
{
	for d in "$@"; do
		log_must mv $away/${d##*/} $d
	done
}

#
# List the exported pools in $devdir.  There must be exactly one, our
# pool, listed with the given state, its raidz vdev with the given state,
# and each given device UNAVAIL.
#
function check_listing # <pool state> <raidz state> [<missing device> ...]
{
	typeset listing state
	typeset -i n

	listing=$(zpool import -d $devdir 2>&1)

	log_note "zpool import -d $devdir:"$'\n'"$listing"

	n=$(echo "$listing" | awk -v p="$TESTPOOL" \
	    '$1 == "pool:" && $2 == p { n++ } END { print n + 0 }')
	if (( n != 1 )); then
		log_fail "$TESTPOOL is listed $n times in $devdir, expected 1"
	fi

	state=$(echo "$listing" | awk '$1 == "state:" { print $2 }')
	if [[ "$state" != "$1" ]]; then
		log_fail "$TESTPOOL is listed $state, expected $1"
	fi

	listed "$listing" raidz$parity-0 $2
	shift 2
	for d in "$@"; do
		listed "$listing" $d UNAVAIL
	done
}

function listed # <listing> <vdev> <state>
{
	if ! echo "$1" | awk -v v="$2" -v s="$3" \
	    '$1 == v && $2 == s { f = 1 } END { exit !f }'; then
		log_fail "$2 is not listed $3"
	fi
}

#
# With the given devices missing, the pool and its raidz vdev are listed
# UNAVAIL, and the pool cannot be imported, read-write or read-only.
#
function check_refused # <missing device> ...
{
	check_listing UNAVAIL UNAVAIL "$@"

	log_mustnot zpool import -o cachefile=none -d $devdir $TESTPOOL
	log_mustnot poolexists $TESTPOOL
	log_mustnot zpool import -o readonly=on -o cachefile=none \
	    -d $devdir $TESTPOOL
	log_mustnot poolexists $TESTPOOL
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
	log_must rm -f $devdir/dev-*
}

log_onexit cleanup

#
# Directories of their own: an import with -d $TEST_BASE_DIR would also
# find the test suite's own file vdevs, which can carry a pool of the same
# name, and the missing disks must be out of the directory searched.
#
log_must mkdir -p $devdir $away

log_assert "parity + 1 missing new disks make a raidz vdev under" \
    "expansion unavailable"

for layout in "${layouts[@]}"; do
	set -- $layout
	typeset -i parity=$1 width=$2 added=$3
	typeset -i total=$((width + added))
	typeset -a disks=() newdisks=() lost=() control=()

	log_note "raidz$parity of $width, attaching $added disks," \
	    "stopped after the scratch step"

	for f in "${!sums[@]}"; do
		unset "sums[$f]"
	done
	for i in {1..$total}; do
		device=$devdir/dev-$i
		log_must truncate -s ${dev_size_mb}M $device
		if (( i <= width )); then
			disks+=("$device")
		else
			newdisks+=("$device")
		fi
	done

	#
	# lost: the last parity + 1 new disks.  control: the last parity
	# new disks, the ones still missing in step 7.  The disks offlined
	# in step 3 are the control disks, so a resilver not finished by
	# the export cannot leave a stale disk next to the missing ones.
	#
	for (( i = added - parity - 1; i < added; i++ )); do
		lost+=("${newdisks[$i]}")
		(( i >= added - parity )) && control+=("${newdisks[$i]}")
	done

	# 1. Create and fill
	log_must zpool create -f -o cachefile=none \
	    -O primarycache=metadata $TESTPOOL raidz$parity "${disks[@]}"
	log_must zfs create -o recordsize=128k $TESTPOOL/fs
	log_must fill_fs /$TESTPOOL/fs 1 160 1048576 1 R
	record_sums

	# 2. Attach, stop soon after the scratch step, check how far it got
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 1
	log_must zpool attach $TESTPOOL raidz$parity-0 "${newdisks[@]}"
	wait_past_scratch
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	log_mustnot zpool detach $TESTPOOL ${newdisks[0]}
	stats=".pools[\"$TESTPOOL\"].raidz_expand_stats"
	reflowed=$(zpool status -jp $TESTPOOL | jq -r "$stats.reflowed")
	to_reflow=$(zpool status -jp $TESTPOOL | jq -r "$stats.to_reflow")
	log_note "paused: $reflowed of $to_reflow bytes copied," \
	    "not counting the scratch area"

	#
	# The pause only takes effect once a txg has recorded some copying,
	# so the copy gets past the scratch area (VDEV_BOOT_SIZE per disk,
	# not counted in reflowed) and, in the first txg after it, up to
	# about (1 + added / width) times the end of the scratch area.  Most
	# of the data must still be laid out only on the old disks.
	#
	typeset -i scratch
	(( scratch = (7 << 19) * total ))
	if (( (reflowed + scratch) * 2 >= to_reflow )); then
		log_fail "paused too late: $reflowed + $scratch" \
		    "of $to_reflow bytes copied"
	fi

	# 3. Offline: parity new disks yes, one more no
	log_must zpool offline $TESTPOOL "${control[@]}"
	log_must check_state $TESTPOOL "" "DEGRADED"
	log_mustnot zpool offline $TESTPOOL ${lost[0]}
	log_must check_vdev_state $TESTPOOL ${lost[0]} "ONLINE"
	online_and_resilver "${control[@]}"
	log_must check_state $TESTPOOL "" "ONLINE"
	log_must check_pool_status $TESTPOOL "expand" "in progress"

	# 4. Export
	log_must_busy zpool export $TESTPOOL
	check_listing ONLINE ONLINE

	# 5. parity + 1 new disks missing
	move_away "${lost[@]}"
	check_refused "${lost[@]}"

	# 7. (and 6.) Back to parity new disks missing
	move_back ${lost[0]}
	if (( parity == 1 )); then
		# 6. One old and one new disk missing: counted alike
		move_away ${disks[0]}
		check_refused ${disks[0]} "${control[@]}"
		move_back ${disks[0]}
	fi
	check_listing DEGRADED DEGRADED "${control[@]}"
	# Control for the read-only refusal in step 5
	log_must zpool import -o readonly=on -o cachefile=none \
	    -d $devdir $TESTPOOL
	log_must check_state $TESTPOOL "" "DEGRADED"
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	verify_sums
	log_must_busy zpool export $TESTPOOL
	log_must zpool import -o cachefile=none -d $devdir $TESTPOOL
	log_must check_state $TESTPOOL "" "DEGRADED"
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	verify_sums

	# 8. Recover and finish
	move_back "${control[@]}"
	online_and_resilver "${control[@]}"
	log_must check_state $TESTPOOL "" "ONLINE"
	log_must check_pool_status $TESTPOOL "expand" "in progress"
	finish_and_verify
done

log_pass "parity + 1 missing new disks make a raidz vdev under" \
    "expansion unavailable"
