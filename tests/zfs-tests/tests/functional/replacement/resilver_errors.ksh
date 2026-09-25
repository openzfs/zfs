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
#	Repair-write errors survive a persisted scan bookmark and import, while
#	an error-free healing pass resumes after import.
#	Unvisited metadata, a dataset whose objset cannot be opened, or an
#	unreadable claimed intent log block retains the original.
#	The recovery override permits retirement without erasing diagnostics.
#
# STRATEGY:
#	1. Fail replacement writes during healing, or let healing succeed.
#	2. Import a healing bookmark, or prevent traversal of metadata whose
#	   children span older TXGs. Failed repairs restart the pass and retain
#	   the original; an error-free pass resumes.
#	3. Remove the faults, finish replacement, and verify the file cold.
#	   Damage every copy of a dataset's objset block, or a claimed log
#	   block, and replace the disk; the unvisited data keeps the
#	   replacement incomplete.
#	4. Allow errorful healing to retire DTLs with the override, while
#	   requiring the diagnostic count to survive import.
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
	set_tunable32 RESILVER_MIN_TIME_MS "$orig_min_time"
	set_tunable64 SCAN_VDEV_LIMIT "$orig_vdev_limit"
	rm -rf "$workdir"
}

log_assert "Incomplete repair retains its error evidence and original device"
orig_ignore=$(get_tunable SCAN_IGNORE_ERRORS)
orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
orig_legacy=$(get_tunable SCAN_LEGACY)
orig_min_time=$(get_tunable RESILVER_MIN_TIME_MS)
orig_vdev_limit=$(get_tunable SCAN_VDEV_LIMIT)
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_errors.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must set_tunable32 SCAN_IGNORE_ERRORS 0
log_must truncate -s 512M "$workdir"/disk-{0,1}
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=64
# Small legacy I/O batches leave a file bookmark before the scan finishes.
log_must set_tunable32 SCAN_LEGACY 1
log_must set_tunable32 RESILVER_MIN_TIME_MS 1
log_must set_tunable64 SCAN_VDEV_LIMIT 131072

for failure in import resume metadata; do
	log_note "Repair failure: $failure"
	log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
	log_must zfs create -o compression=off -o atime=off -o recordsize=128k \
	    "$TESTPOOL1/$TESTFS"
	mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
	# A later indirect block can reference children born in earlier TXGs.
	# Losing it must not limit retained DTLs to the parent's birth TXG.
	log_must dd if="$workdir/expected" of="$mntpnt/file" bs=1M count=32
	sync_pool "$TESTPOOL1"
	log_must dd if="$workdir/expected" of="$mntpnt/file" bs=1M \
	    skip=32 seek=32 count=32 conv=notrunc
	sync_pool "$TESTPOOL1"
	object=$(get_objnum "$mntpnt/file")
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace "$TESTPOOL1" \
	    "$workdir/disk-0" "$workdir/disk-1"
	if [[ "$failure" != resume ]]; then
		# Suppress the write and report failure, leaving missing bytes
		# and DTLs. An I/O error alone can be injected after bytes are
		# written.
		log_must zinject -d "$workdir/disk-1" -e noop -T write \
		    -f 100 "$TESTPOOL1"
		log_must zinject -d "$workdir/disk-1" -e io -T write \
		    -f 100 "$TESTPOOL1"
	fi

	if [[ "$failure" != metadata ]]; then
		log_must zinject -d "$workdir/disk-0" -D 10:1 -T read "$TESTPOOL1"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	if [[ "$failure" != metadata ]]; then
		# Freeze and sync before zdb opens the MOS; otherwise it can see
		# a completed scan instead of the prefix to import. The MOS scan
		# array is dsl_scan_phys_t: bookmark object and block ID are
		# words 21 and 23. It resumes only while an identical
		# org.openzfs:scan_healing copy exists, which a failed repair
		# removes.
		resumable=0
		[[ "$failure" == resume ]] && resumable=1
		for ((i = 0; i < 30; i++)); do
			sync_pool "$TESTPOOL1"
			log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
			sync_pool "$TESTPOOL1"
			log_must eval "zdb -dddd '$TESTPOOL1' 1 >'$workdir/scan'"
			# shellcheck disable=SC2016 # awk field references
			log_must awk '$1 == "scan" { print }' "$workdir/scan"
			if awk -v object="$object" -v resumable="$resumable" '
			    $1 == "scan" {
				found = ($4 == 1 && $23 == object && $25 > 0)
				$1 = ""; scan = $0
			    }
			    $1 == "org.openzfs:scan_healing" { $1 = ""; copy = $0 }
			    END {
				exit !(found && (copy == scan) == resumable)
			    }' "$workdir/scan"; then
				break
			fi
			log_must is_pool_resilvering "$TESTPOOL1"
			log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		done
		(( i < 30 )) || log_fail "no persisted $failure file bookmark"
		log_must is_pool_resilvering "$TESTPOOL1"
		log_must zinject -c all
		restarts=$(zpool history -i "$TESTPOOL1" |
		    grep -c "scan aborted, restarting")
		log_must zpool export "$TESTPOOL1"
		log_must zpool import -d "$workdir" "$TESTPOOL1"
		sync_pool "$TESTPOOL1"
		log_must is_pool_replacing "$TESTPOOL1"
		after=$(zpool history -i "$TESTPOOL1" |
		    grep -c "scan aborted, restarting")
		if [[ "$failure" == resume ]]; then
			log_must test "$after" -eq "$restarts"
		else
			log_must test "$after" -gt "$restarts"
		fi
	else
		log_must zpool wait -t resilver "$TESTPOOL1"
		log_must is_pool_replacing "$TESTPOOL1"
		log_must zinject -c all
	fi

	if [[ "$failure" == metadata ]]; then
		# Drop cached metadata before blocking file-child traversal.
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		log_must zpool export "$TESTPOOL1"
		log_must zpool import -d "$workdir" "$TESTPOOL1"
		sync_pool "$TESTPOOL1"
		log_must is_pool_resilvering "$TESTPOOL1"
		log_must zinject -a -t data -l 1 -e io -f 100 "$mntpnt/file"
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		log_must zpool wait -t resilver "$TESTPOOL1"
		log_must zinject
		count=$(zinject | awk '/^ *[0-9]/ { print $NF }')
		(( count > 0 )) || log_fail "metadata injection did not fire"
		log_must is_pool_replacing "$TESTPOOL1"
		log_must zinject -c all
	fi

	if [[ "$failure" == metadata ]]; then
		# Request the remaining healing work.
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		log_must zpool resilver "$TESTPOOL1"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t replace "$TESTPOOL1"
	if [[ "$failure" == resume ]]; then
		# A restart requested asynchronously would appear by now.
		log_must test "$(zpool history -i "$TESTPOOL1" |
		    grep -c "scan aborted, restarting")" -eq "$restarts"
	fi
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	destroy_pool "$TESTPOOL1"
done

log_note "Unreadable objset"
# Scanning opens a filesystem's objset before visiting its blocks. A failed
# open must not skip the dataset: traversal still reaches the damaged root
# block, and the pass cannot retire the missing writes.
log_must truncate -s 0 "$workdir"/disk-{0,1}
log_must truncate -s 512M "$workdir"/disk-{0,1}
log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
log_must zfs create -o compression=off -o atime=off "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must cp "$workdir/expected" "$mntpnt/file"
dsobj=$(get_prop objsetid "$TESTPOOL1/$TESTFS")
log_must zpool export "$TESTPOOL1"
log_must eval "zdb -e -p '$workdir' -dddd '$TESTPOOL1' $dsobj >'$workdir/ds'"
# Overwrite each copy: DVAs print as <vdev:offset:asize> in hex, and allocated
# space starts after the 4 MiB of front labels and boot area.
# shellcheck disable=SC2016 # awk field references
awk '$1 == "bp" {
    for (i = 3; i <= NF; i++) if ($i ~ /^DVA/) {
	split($i, dva, /[<:>]/); print dva[3], dva[4] } }' \
    "$workdir/ds" >"$workdir/dvas" || log_fail "cannot parse objset DVAs"
(( $(wc -l <"$workdir/dvas") > 0 )) || log_fail "no objset DVAs for $dsobj"
while read -r offset asize; do
	log_must dd if=/dev/zero of="$workdir/disk-0" bs=512 conv=notrunc \
	    seek=$(( (0x$offset + 4194304) / 512 )) count=$(( 0x$asize / 512 ))
done <"$workdir/dvas"
log_must zpool import -N -d "$workdir" "$TESTPOOL1"
log_must zpool replace "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-1"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must eval "kstat dbgmsg | grep -q 'cannot open objset of dataset $dsobj:'"
log_must is_pool_replacing "$TESTPOOL1"
destroy_pool "$TESTPOOL1"

log_note "Unreadable claimed log block"
# A log claimed at import is scanned until it is replayed. Its first block is
# not the end of the chain, so failing to read it hides the rest of the log.
log_must truncate -s 0 "$workdir"/disk-{0,1}
log_must truncate -s 512M "$workdir"/disk-{0,1}
log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
log_must zfs create -o compression=off -o atime=off -o sync=always \
    "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
# Records written after the freeze need a ZIL header already on disk.
log_must dd if=/dev/zero of="$mntpnt/sync" conv=fdatasync,fsync bs=1 count=1
log_must zpool freeze "$TESTPOOL1"
for i in 1 2 3 4; do
	log_must dd if=/dev/urandom of="$mntpnt/log-$i" bs=4k count=1
done
log_must zpool export "$TESTPOOL1"
# Frozen labels need -f. Without mounting, the log stays claimed.
log_must zpool import -f -N -d "$workdir" "$TESTPOOL1"
log_must eval "zdb -ivvvvv '$TESTPOOL1/$TESTFS' >'$workdir/zil'"
# shellcheck disable=SC2016 # awk field references
awk '/Block seqno .* already claimed/ && !done {
    for (i = 1; i <= NF; i++) if ($i ~ /^DVA/) {
	split($i, dva, /[<:>]/); print dva[3], dva[4] }
    done = 1 }' "$workdir/zil" >"$workdir/dvas" ||
    log_fail "cannot parse log block DVAs"
(( $(grep -c "already claimed" "$workdir/zil") > 1 )) ||
    log_fail "log has no claimed block past its first"
(( $(wc -l <"$workdir/dvas") > 0 )) || log_fail "no log block DVAs"
while read -r offset asize; do
	log_must dd if=/dev/zero of="$workdir/disk-0" bs=512 conv=notrunc \
	    seek=$(( (0x$offset + 4194304) / 512 )) count=$(( 0x$asize / 512 ))
done <"$workdir/dvas"
# Flush the ARC with a handler that matches nothing, so the scan reads disk.
log_must zinject -a -b 7777:0:0:0 -e io "$TESTPOOL1"
log_must zinject -c all
log_must zpool replace "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-1"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must is_pool_replacing "$TESTPOOL1"
destroy_pool "$TESTPOOL1"

log_note "Recovery override diagnostics"
# Attach keeps the original available after deliberately authorizing incomplete
# repair. Export/import makes the logical metadata injection reach disk reads.
log_must truncate -s 0 "$workdir"/disk-{0,1}
log_must truncate -s 512M "$workdir"/disk-{0,1}
log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
log_must zfs create -o compression=off -o atime=off -o recordsize=128k \
    "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must cp "$workdir/expected" "$mntpnt/file"
sync_pool "$TESTPOOL1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must set_tunable32 SCAN_IGNORE_ERRORS 1
log_must zpool attach "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-1"
log_must zinject -a -t data -l 1 -e io -f 100 "$mntpnt/file"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must set_tunable32 SCAN_IGNORE_ERRORS 0
log_must zinject -c all
log_must check_pool_status "$TESTPOOL1" scan \
    "resilvered .*with [1-9][0-9]* errors" true
# Completion status alone does not show that the override retired missing DTLs.
log_must eval "zdb -ddd '$TESTPOOL1' >'$workdir/dtl'"
# shellcheck disable=SC2016 # awk field references
log_must awk -v leaf="$workdir/disk-1" '
    $2 ~ /^\[DTL-/ {
        selected = ($1 == leaf)
        if (selected) found = 1
    }
    selected && $1 == "missing" { missing = 1 }
    END { exit (!found || missing) }' "$workdir/dtl"
# Keep the known-good original: the override deliberately lost repairs.
log_must zpool offline "$TESTPOOL1" "$workdir/disk-1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$workdir" "$TESTPOOL1"
log_must check_pool_status "$TESTPOOL1" scan \
    "resilvered .*with [1-9][0-9]* errors" true
log_must cmp "$workdir/expected" "$mntpnt/file"

log_pass "Incomplete repair retains its error evidence and original device"
