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
#	Verification hands missing writes to healing when a device returns,
#	including while resuming a paused scrub and with the recovery override.
#
# STRATEGY:
#	1. Fail mirror-child writes, then make its backing file unavailable.
#	2. Suspend a legacy scrub at a persisted file bookmark, or pause an
#	   error scrub with a real log entry. Also test an empty error log.
#	3. Return the child through reopen -n or a scrub request.
#	4. Finish verification with the recovery override enabled and require
#	   automatic healing, without requesting a resilver.
#	5. Offline the original and verify the data with a cold read.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	# Destroy before restoring progress so cleanup cannot finish repair.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_IGNORE_ERRORS "$orig_ignore"
	set_tunable32 SCAN_SUSPEND_PROGRESS "$orig_suspend"
	set_tunable32 SCAN_LEGACY "$orig_legacy"
	set_tunable32 SCRUB_MIN_TIME_MS "$orig_min_time"
	set_tunable64 SCAN_VDEV_LIMIT "$orig_vdev_limit"
	rm -rf "$workdir"
}

log_assert "Verification hands a returning device's missing writes to healing"
orig_ignore=$(get_tunable SCAN_IGNORE_ERRORS)
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
orig_legacy=$(get_tunable SCAN_LEGACY)
orig_min_time=$(get_tunable SCRUB_MIN_TIME_MS)
orig_vdev_limit=$(get_tunable SCAN_VDEV_LIMIT)
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_dtl_retirement.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=64
# Bound legacy I/O so the sync waiter can leave a real, persisted file prefix.
log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable32 SCRUB_MIN_TIME_MS 1
log_must set_tunable64 SCAN_VDEV_LIMIT 131072

for mode in empty errorscrub resume reopen; do
	log_note "Return during verification: $mode"
	log_must set_tunable32 SCAN_IGNORE_ERRORS 0
	log_must truncate -s 0 "$workdir"/disk-{0,1}
	log_must truncate -s 512M "$workdir"/disk-{0,1}
	log_must zpool create -f "$TESTPOOL1" mirror "$workdir"/disk-{0,1}
	log_must zfs create -o compression=off -o atime=off -o recordsize=128k \
	    "$TESTPOOL1/$TESTFS"
	mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
	# Suppress the write itself as well as reporting failure; the returned
	# device must lack bytes, not merely carry a conservative missing DTL.
	log_must zinject -d "$workdir/disk-1" -e noop -T write -f 100 "$TESTPOOL1"
	log_must zinject -d "$workdir/disk-1" -e io -T write -f 100 "$TESTPOOL1"
	log_must cp "$workdir/expected" "$mntpnt/file"
	sync_pool "$TESTPOOL1"
	object=$(get_objnum "$mntpnt/file")
	log_must zinject -c all
	# Export closes the file vdev; renaming alone leaves it open.
	log_must zpool export "$TESTPOOL1"
	log_must mv "$workdir/disk-1" "$workdir/absent"
	log_must zpool import -d "$workdir/disk-0" "$TESTPOOL1"
	log_must check_state "$TESTPOOL1" "$workdir/disk-1" unavail

	if [[ "$mode" == resume || "$mode" == reopen ]]; then
		log_must zinject -d "$workdir/disk-0" -D 10:1 -T read "$TESTPOOL1"
		log_must zpool scrub "$TESTPOOL1"
		# Suspend and sync before zdb opens the pool. The scan array is
		# dsl_scan_phys_t, preceded by "scan ="; require SCRUB/SCANNING
		# with a nonzero file bookmark, not merely a started scrub.
		for ((i = 0; i < 30; i++)); do
			sync_pool "$TESTPOOL1"
			log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
			sync_pool "$TESTPOOL1"
			log_must eval "zdb -dddd '$TESTPOOL1' 1 >'$workdir/scan'"
			if awk -v object="$object" '$1 == "scan" {
			    found = ($3 == 1 && $4 == 1 &&
			        $23 == object && $25 > 0)
			} END { exit !found }' "$workdir/scan"; then
				break
			fi
			log_must is_pool_scrubbing "$TESTPOOL1"
			log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		done
		(( i < 30 )) || log_fail "no persisted scrub bookmark in the file"
		scrub_max=$(awk '$1 == "scan" { print $7 }' "$workdir/scan")
		log_must zinject -c all
		if [[ "$mode" == resume ]]; then
			log_must zpool scrub -p "$TESTPOOL1"
			log_must is_pool_scrub_paused "$TESTPOOL1"
		fi
	else
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		if [[ "$mode" == errorscrub ]]; then
			# An empty log starts no scan. Require a failed read
			# to create an entry before testing error-scrub resume.
			log_must zinject -a -m -t data -e io -f 100 "$mntpnt/file"
			log_mustnot eval "cat '$mntpnt/file' >/dev/null"
			sync_pool "$TESTPOOL1"
			log_must zinject -c all
			log_must zpool scrub -e "$TESTPOOL1"
			log_must zpool scrub -p "$TESTPOOL1"
			log_must is_pool_error_scrub_paused "$TESTPOOL1"
		fi
	fi

	log_must mv "$workdir/absent" "$workdir/disk-1"
	case "$mode" in
	reopen) log_must zpool reopen -n "$TESTPOOL1" ;;
	resume) log_must zpool scrub "$TESTPOOL1" ;;
	*) log_must zpool scrub -e "$TESTPOOL1" ;;
	esac
	if [[ "$mode" == errorscrub ]]; then
		log_must is_pool_error_scrubbing "$TESTPOOL1"
	elif [[ "$mode" == empty ]]; then
		# libzfs accepts an empty log without starting an error scrub.
		log_mustnot is_pool_error_scrubbing "$TESTPOOL1"
	else
		log_must is_pool_scrubbing "$TESTPOOL1"
	fi
	# Rewrite exported labels before inspecting the returned leaf with zdb.
	log_must zpool set comment="returned scrub device" "$TESTPOOL1"
	sync_pool "$TESTPOOL1"
	log_must eval "zdb -ddd '$TESTPOOL1' >'$workdir/dtl'"
	# shellcheck disable=SC2016 # awk field references
	log_must awk -v leaf="$workdir/disk-1" '
	    $2 ~ /^\[DTL-/ { selected = ($1 == leaf) }
	    selected && $1 == "missing" { found = 1 }
	    END { exit !found }' "$workdir/dtl"
	# The override may ignore healing errors, but cannot give verification
	# authority to retire the missing prefix.
	log_must set_tunable32 SCAN_IGNORE_ERRORS 1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	if [[ "$mode" == errorscrub ]]; then
		for ((i = 0; i < 30; i++)); do
			sync_pool "$TESTPOOL1"
			is_pool_error_scrubbing "$TESTPOOL1" || break
		done
		(( i < 30 )) || log_fail "error scrub did not complete"
	else
		log_must zpool wait -t scrub "$TESTPOOL1"
		if [[ "$mode" != empty ]]; then
			# Preemption must not count as completed verification.
			last_scrubbed=$(get_pool_prop last_scrubbed_txg "$TESTPOOL1")
			log_must test "$last_scrubbed" -eq "$scrub_max"
		fi
	fi
	# A post-scrub DTL snapshot races automatic healing. Require a real
	# resilver and a cold read without the original to detect premature
	# retirement even if both scans report completion.
	log_must zpool wait -t resilver "$TESTPOOL1"
	log_must is_pool_resilvered "$TESTPOOL1"
	log_must set_tunable32 SCAN_IGNORE_ERRORS 0
	log_must zpool offline "$TESTPOOL1" "$workdir/disk-0"
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	destroy_pool "$TESTPOOL1"
done

log_pass "Verification handed the missing writes to automatic healing"
