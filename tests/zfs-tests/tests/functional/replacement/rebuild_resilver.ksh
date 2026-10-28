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

. "$STF_SUITE/include/libtest.shlib"

#
# DESCRIPTION:
# Healing blocked by sequential reconstruction starts when reconstruction ends.
#
# STRATEGY:
# Make a mirror child miss writes and bring it online during a held rebuild.
# Complete or cancel the rebuild with automatic verification disabled, including
# cancellation with another rebuild active. Require automatic healing and read
# the data after import with the original source offline. With verification
# enabled, complete two rebuilds in one TXG and require their full scrub to
# repair the child without being preempted.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$fault" ]] && log_must zinject -c "$fault"
	[[ -n "$sync_pid" ]] && wait "$sync_pid"
	destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	log_must set_tunable32 VDEV_MIN_MS_COUNT "$ms_count"
	rm -rf "$workdir"
}

function sync_delayed
{
	zinject | awk -v id="$fault" \
	    '$1 == id && $8 > 0 { hit = 1 } END { exit !hit }'
}

log_assert "Rebuild completion and cancellation resume outstanding healing"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
ms_count=$(get_tunable VDEV_MIN_MS_COUNT) ||
    log_fail "cannot read VDEV_MIN_MS_COUNT"
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_resilver.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
# Use two metaslabs per vdev to align the workers' final I/O waits.
log_must set_tunable32 VDEV_MIN_MS_COUNT 2
log_must truncate -s 600M "$workdir"/disk{0..5}
log_must file_write -o create -f "$workdir/expected" \
    -b 1048576 -c 4 -d 65

typeset -a rebuild_vdevs
for finish in complete cancel multiple scrub; do
	log_note "Repair after rebuild $finish"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
	[[ "$finish" == scrub ]] &&
	    log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
	log_must zpool create -f -O compression=off -O primarycache=metadata \
	    -m "$workdir/mnt" "$TESTPOOL1" mirror "$workdir"/disk{0,1}
	log_must zpool offline "$TESTPOOL1" "$workdir/disk1"
	log_must cp "$workdir/expected" "$workdir/mnt/file"
	log_must zpool sync "$TESTPOOL1"
	rebuild_vdevs=("$workdir/disk2")
	if [[ "$finish" == multiple || "$finish" == scrub ]]; then
		rebuild_vdevs+=("$workdir/disk3")
	fi
	log_must zpool add -f "$TESTPOOL1" "${rebuild_vdevs[@]}"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool attach -s "$TESTPOOL1" \
	    "$workdir/disk2" "$workdir/disk4"
	log_must is_pool_resilvering "$TESTPOOL1"
	if [[ "$finish" == multiple || "$finish" == scrub ]]; then
		log_must zpool attach -s "$TESTPOOL1" \
		    "$workdir/disk3" "$workdir/disk5"
		log_must is_pool_resilvering "$TESTPOOL1"
	fi
	log_must zpool online "$TESTPOOL1" "$workdir/disk1"
	log_must zpool sync "$TESTPOOL1"
	if [[ "$finish" == cancel || "$finish" == multiple ]]; then
		log_must zpool detach "$TESTPOOL1" "$workdir/disk4"
		if [[ "$finish" == multiple ]]; then
			log_must is_pool_resilvering "$TESTPOOL1"
			log_must zpool detach "$TESTPOOL1" "$workdir/disk5"
		fi
	fi
	if [[ "$finish" == scrub ]]; then
		# Admit both workers' I/O while a prior TXG is still syncing.
		fault=$(zinject -q -d "$workdir/disk0" -D 5000:100 \
		    -T write "$TESTPOOL1") || log_fail "cannot delay sync"
		zfs set org.openzfs:test=sync "$TESTPOOL1" &
		sync_pid=$!
		for ((i = 0; i < 10; i++)); do
			sync_delayed && break
			sleep 1
		done
		(( i < 10 )) || log_fail "sync write was not delayed"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	if [[ "$finish" == scrub ]]; then
		log_must wait "$sync_pid"
		sync_pid=
		log_must zinject -c "$fault"
		fault=
	fi

	func=2
	activity=resilver
	result="resilvered .* with 0 errors"
	if [[ "$finish" == scrub ]]; then
		func=1
		activity=scrub
		result="scrub repaired .* with 0 errors"
	fi
	for ((i = 0; i < 30; i++)); do
		zpool history -i "$TESTPOOL1" |
		    grep -q "scan setup func=$func " && break
		sleep 1
	done
	(( i < 30 )) || log_fail "automatic $activity did not start"
	log_must zpool wait -t "$activity" "$TESTPOOL1"
	log_must check_pool_status "$TESTPOOL1" scan "$result"
	if [[ "$finish" == scrub ]]; then
		history=$(zpool history -i "$TESTPOOL1") ||
		    log_fail "cannot read scan history"
		log_mustnot grep -q 'scan setup func=2 ' <<< "$history"
		awk '
		    /rebuild.* complete$/ {
		        print
		        sub(/.*txg:/, "")
		        sub(/\].*/, "")
		        if (++n == 1)
		            txg = $0
		        if ($0 != txg)
		            different = 1
		    }
		    END { exit (n != 2 || different) }
		' <<< "$history" ||
		    log_fail "rebuilds did not complete in one TXG"
	fi
	log_must zpool offline "$TESTPOOL1" "$workdir/disk0"
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$workdir/mnt/file"
	destroy_pool "$TESTPOOL1"
done

log_pass "Outstanding healing resumes without preempting verification"
