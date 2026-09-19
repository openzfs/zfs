#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A sequential rebuild keeps its original txg bounds, including after
# import, and does not retire missing writes newer than those bounds.
#
# STRATEGY:
# Suspend a mirror or dRAID rebuild and record its saved bounds. Take the
# new device offline, write and sync new data, then return the device.
# Require a missing txg beyond the saved interval. Complete the rebuild,
# with and without an intervening export/import, and require its saved
# bounds to be unchanged and the later missing txg to remain in the DTL.
# The device is writable before progress resumes, so failed rebuild writes
# cannot keep unrelated ranges in the DTL and mask incorrect retirement.
# Heal the remaining txgs and verify the data without the original device.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	destroy_pool $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $scan_suspend
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $rebuild_scrub
	rm -rf "$workdir"
}

function saved_bounds
{
	zpool sync $TESTPOOL1 || log_fail "cannot sync $TESTPOOL1"
	zdb -dddd $TESTPOOL1 $top_zap > "$workdir/rebuild" ||
	    log_fail "cannot read saved rebuild state"
	# The record begins with state, last offset, min txg and max txg.
	awk '$1 == "org.openzfs:vdev_rebuild" { print $5, $6 }' \
	    "$workdir/rebuild"
}

function missing_ranges
{
	zpool sync $TESTPOOL1 || log_fail "cannot sync $TESTPOOL1"
	zdb -ddd $TESTPOOL1 > "$workdir/dtls" ||
	    log_fail "cannot read DTLs"
	awk -v leaf="$workdir/disk-3" '
	    $2 ~ /^\[DTL-/ { selected = ($1 == leaf) }
	    selected && $1 == "missing" {
		split($2, r, /[\[,)]/); print r[2], r[3] }' "$workdir/dtls"
}

log_assert "Rebuild completion preserves newer missing txgs"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_late_writes.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must file_write -o create -f "$workdir/base.expected" \
    -b 1048576 -c 64 -d 65
log_must file_write -o create -f "$workdir/late.expected" \
    -b 1048576 -c 1 -d 90

for layout in mirror draid; do
	for reimport in no yes; do
		log_note "$layout rebuild, export/import: $reimport"
		log_must truncate -s 1G "$workdir"/disk-{0..3}
		if [[ $layout == mirror ]]; then
			log_must zpool create -f -O compression=off \
			    -m "$workdir/mnt" $TESTPOOL1 "$workdir/disk-0"
		else
			log_must zpool create -f -O compression=off \
			    -m "$workdir/mnt" $TESTPOOL1 draid1:2d:3c:0s \
			    "$workdir"/disk-{0..2}
		fi
		log_must file_write -o create -f "$workdir/mnt/base" \
		    -b 1048576 -c 64 -d 65
		log_must zpool sync $TESTPOOL1
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		if [[ $layout == mirror ]]; then
			log_must zpool attach -s $TESTPOOL1 \
			    "$workdir/disk-0" "$workdir/disk-3"
		else
			log_must zpool replace -s $TESTPOOL1 \
			    "$workdir/disk-0" "$workdir/disk-3"
		fi
		log_must is_pool_resilvering $TESTPOOL1
		top_zap=$(zdb -C $TESTPOOL1 |
		    awk '/com.delphix:vdev_zap_top:/ { print $2 }')
		[[ $top_zap == +([0-9]) ]] || log_fail "missing top vdev ZAP"
		bounds=$(saved_bounds)
		set -A txgs $bounds
		(( ${#txgs[@]} == 2 && txgs[0] < txgs[1] )) ||
		    log_fail "invalid rebuild bounds: $bounds"

		log_must zpool offline $TESTPOOL1 "$workdir/disk-3"
		log_must file_write -o create -f "$workdir/mnt/late" \
		    -b 1048576 -c 1 -d 90
		log_must zpool sync $TESTPOOL1
		log_must zpool online $TESTPOOL1 "$workdir/disk-3"
		late=$(missing_ranges | awk 'END { if (NF) print $2 - 1 }')
		[[ $late == +([0-9]) ]] && (( late >= txgs[1] )) ||
		    log_fail "no missing txg beyond the rebuild's upper bound"
		log_note "saved bounds ($bounds), later missing txg $late"

		if [[ $reimport == yes ]]; then
			log_must zpool export $TESTPOOL1
			log_must zpool import -d "$workdir" $TESTPOOL1
		fi
		now=$(saved_bounds)
		[[ $now == "$bounds" ]] ||
		    log_fail "restart changed bounds from ($bounds) to ($now)"
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		log_must zpool wait -t resilver $TESTPOOL1
		log_must eval "zpool history -i $TESTPOOL1 |
		    grep -q 'rebuild.*complete'"
		now=$(saved_bounds)
		if [[ $now != "$bounds" ]]; then
			log_note "remaining missing ranges: $(missing_ranges)"
			log_fail "rebuild changed bounds from ($bounds) to ($now)"
		fi
		missing_ranges | awk -v txg=$late \
		    '$1 <= txg && txg < $2 { found = 1 } END { exit !found }' ||
		    log_fail "rebuild completion retired later missing txg $late"

		log_must zpool resilver $TESTPOOL1
		# The resilver request takes effect in syncing context.
		log_must zpool sync $TESTPOOL1
		log_must zpool wait -t resilver $TESTPOOL1
		ranges=$(missing_ranges)
		[[ -z "$ranges" ]] ||
		    log_fail "healing left missing ranges: $ranges"
		if [[ $layout == mirror ]]; then
			log_must zpool offline $TESTPOOL1 "$workdir/disk-0"
		else
			log_must zpool wait -t replace $TESTPOOL1
			log_mustnot is_pool_replacing $TESTPOOL1
		fi
		log_must zpool export $TESTPOOL1
		log_must zpool import -d "$workdir" $TESTPOOL1
		log_must cmp "$workdir/base.expected" "$workdir/mnt/base"
		log_must cmp "$workdir/late.expected" "$workdir/mnt/late"
		log_must zpool destroy $TESTPOOL1
	done
done

log_pass "Rebuilds preserved later missing txgs for successful healing"
