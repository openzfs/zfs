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

# shellcheck disable=SC1091
. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Reopening a missing leaf accounts for its lost healing coverage,
#	including when the command which reopens it is rejected.
#
# STRATEGY:
#	1. Make an incompletely written mirror leaf unavailable.
#	2. Persist error-free healing on newer writes in a second mirror.
#	3. Return through online, scrub, error scrub, reopen -n, or clear.
#	4. Check deferred participation.
#	5. Complete healing and cold-read with the original first leaf offline.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	# Preserve the failed scan state until the pool is destroyed.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS "$orig_suspend"
	set_tunable32 SCAN_LEGACY "$orig_legacy"
	set_tunable32 RESILVER_MIN_TIME_MS "$orig_min_time"
	set_tunable32 RESILVER_DEFER_PERCENT "$orig_defer"
	set_tunable64 SCAN_VDEV_LIMIT "$orig_vdev_limit"
	rm -rf "$workdir"
}

log_assert "Every reopen path accounts for a returning leaf's missed prefix"
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
orig_legacy=$(get_tunable SCAN_LEGACY)
orig_min_time=$(get_tunable RESILVER_MIN_TIME_MS)
orig_defer=$(get_tunable RESILVER_DEFER_PERCENT)
orig_vdev_limit=$(get_tunable SCAN_VDEV_LIMIT)
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_reopen.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
# Bound legacy I/O so a sync waiter can stop traversal inside the newer file.
log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable32 RESILVER_MIN_TIME_MS 1
log_must set_tunable32 RESILVER_DEFER_PERCENT 0
log_must set_tunable64 SCAN_VDEV_LIMIT 131072
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=8

for defer in disabled enabled; do
for command in online scrub errorscrub reopen clear; do
	log_note "Return through $command with resilver_defer=$defer"
	log_must truncate -s 0 "$workdir"/disk-{0..3}
	log_must truncate -s 512M "$workdir"/disk-{0..3}
	log_must zpool create -f -o feature@resilver_defer="$defer" \
	    "$TESTPOOL1" mirror "$workdir"/disk-{0,1}
	log_must zfs create -o compression=off -o atime=off "$TESTPOOL1/early"
	mntpnt=$(get_prop mountpoint "$TESTPOOL1/early")
	# A completion error can leave valid bytes; noop also drops the write.
	log_must zinject -d "$workdir/disk-1" -e noop -T write -f 100 "$TESTPOOL1"
	log_must zinject -d "$workdir/disk-1" -e io -T write -f 100 "$TESTPOOL1"
	log_must cp "$workdir/expected" "$mntpnt/file"
	sync_pool "$TESTPOOL1"
	log_must zinject -c all
	# Reopen retains a file vdev's descriptor. Export first so hiding its
	# path really makes it unavailable on the following import.
	log_must zpool export "$TESTPOOL1"
	log_must mv "$workdir/disk-1" "$workdir/absent"
	log_must zpool import -d "$workdir/disk-0" "$TESTPOOL1"
	log_must check_state "$TESTPOOL1" "$workdir/disk-1" unavail
	# Repair only newer writes on a second mirror. An attach-driven scan
	# would attempt the unavailable leaf and already count errors.
	log_must zpool add -f "$TESTPOOL1" mirror "$workdir"/disk-{2,3}
	log_must zpool set allocating=off "$TESTPOOL1" mirror-0
	log_must zfs create -o compression=off -o atime=off -o recordsize=128k \
	    "$TESTPOOL1/late"
	late_mntpnt=$(get_prop mountpoint "$TESTPOOL1/late")
	log_must zinject -d "$workdir/disk-3" -e noop -T write -f 100 "$TESTPOOL1"
	log_must zinject -d "$workdir/disk-3" -e io -T write -f 100 "$TESTPOOL1"
	log_must dd if=/dev/urandom of="$late_mntpnt/file" bs=1M count=64
	sync_pool "$TESTPOOL1"
	log_must zinject -c all
	object=$(get_objnum "$late_mntpnt/file")
	objset=$(get_prop objsetid "$TESTPOOL1/late")
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool reopen "$TESTPOOL1"
	log_must zinject -d "$workdir/disk-2" -D 10:1 -T read "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	# Freeze and sync the MOS so zdb cannot race scan completion.
	# The dsl_scan_phys_t array follows "scan =" in the awk fields.
	# Require RESILVER/SCANNING, zero errors, and a nonzero file bookmark.
	for ((i = 0; i < 30; i++)); do
		sync_pool "$TESTPOOL1"
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		sync_pool "$TESTPOOL1"
		log_must eval "zdb -dddd '$TESTPOOL1' 1 >'$workdir/scan'"
		if awk -v object="$object" -v objset="$objset" '$1 == "scan" {
		    found = ($3 == 2 && $4 == 1 && $16 == 0 &&
		        $22 == objset && $23 == object && $25 > 0)
		} END { exit !found }' "$workdir/scan"; then
			break
		fi
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	done
	(( i < 30 )) || log_fail "no error-free bookmark in the newer dataset"
	scan_min=$(awk '$1 == "scan" { print $6 }' "$workdir/scan")
	log_must eval "zdb -ddd '$TESTPOOL1' >'$workdir/dtl'"
	dtl_max=$(awk -v leaf="$workdir/disk-1" '
	    $2 ~ /^\[DTL-/ { selected = ($1 == leaf) }
	    selected && $1 == "missing" {
	        split($2, range, /[,\)]/)
	        if (range[2] > max) max = range[2]
	    } END { print max + 0 }' "$workdir/dtl")
	log_note "returning DTL maximum=$dtl_max, active scan minimum=$scan_min"
	(( dtl_max > 0 && dtl_max <= scan_min )) ||
	    log_fail "the active pass did not exclude the returning leaf's data"
	log_must mv "$workdir/absent" "$workdir/disk-1"
	case "$command" in
	online) log_must zpool online "$TESTPOOL1" "$workdir/disk-1" ;;
	scrub) log_mustnot zpool scrub "$TESTPOOL1" ;;
	errorscrub) log_mustnot zpool scrub -e "$TESTPOOL1" ;;
	reopen) log_must zpool reopen -n "$TESTPOOL1" ;;
	clear) log_must zpool clear "$TESTPOOL1" ;;
	esac
	log_must check_state "$TESTPOOL1" "$workdir/disk-1" online
	# Rewrite exported labels before inspecting the returned leaf with zdb.
	log_must zpool set comment="returned during healing" "$TESTPOOL1"
	sync_pool "$TESTPOOL1"
	if [[ "$defer" == enabled ]]; then
		log_must eval "zdb -C '$TESTPOOL1' >'$workdir/config'"
		# shellcheck disable=SC2016 # awk field references
		log_must awk -v leaf="'$workdir/disk-1'" '
		    /path:/ { selected = ($2 == leaf) }
		    selected && /com.datto:resilver_defer/ { found = 1 }
		    END { exit !found }' "$workdir/config"
	fi
	log_must zinject -c all
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver "$TESTPOOL1"
	# Exclude both the known-good original and cached bytes from this read.
	log_must zpool offline "$TESTPOOL1" "$workdir/disk-0"
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	destroy_pool "$TESTPOOL1"
done
done

log_pass "Every reopen path accounted for the returning leaf's missed prefix"
