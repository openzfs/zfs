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
#	Split indirect repair validates the full block before overwriting any
#	candidate, including copies beneath replacing and spare vdevs.
#
# STRATEGY:
#	1. Remove a vdev with 128K file blocks using 32K removal segments.
#	2. Replace the remaining vdev, failing repair writes to the target.
#	3. Remove the injection and verify that a mapped file
#	   segment is present on the original but missing from the target.
#	4. Fail reads of the original. An ordinary read must fail without
#	   copying the target's bytes over the original's only valid segment.
#	5. Retry healing with target reads failing but writes allowed.
#	6. Reverse the replacement. Fail reads of the DTL-clean source; Direct
#	   I/O and ordinary cold reads must succeed from the DTL-dirty target.
#	   Then corrupt the source segment and recover through an ordinary cold
#	   read, which must also repair the source before the scan resumes.
#	7. Repeat beneath a mirror with replacing and spare children. With no
#	   valid combination, verify a failed read leaves all copies intact.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	# Destroy before restoring progress so cleanup cannot finish repair.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS \
	    "$ORIG_SCAN_SUSPEND_PROGRESS" >/dev/null 2>&1
	set_tunable32 REMOVE_MAX_SEGMENT \
	    "$ORIG_REMOVE_MAX_SEGMENT" >/dev/null 2>&1
	rm -f "${VDEV_FILES[0]}" "${VDEV_FILES[1]}" "${VDEV_FILES[2]}" \
	    "$SPARE_VDEV_FILE"
	rm -rf "$workdir"
}

log_assert "Split indirect reconstruction preserves and validates repair copies"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
workdir=$(mktemp -d "$TEST_BASE_DIR/indirect_repair.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must zinject -c all
# Removal needs free space beyond the pool's minimum slop reservation.
log_must truncate -s 512M \
    "${VDEV_FILES[0]}" "${VDEV_FILES[1]}" "$SPARE_VDEV_FILE"
log_must zpool create -f -o feature@resilver_defer=disabled \
    "$TESTPOOL1" "${VDEV_FILES[1]}"
# Keep file data out of the ARC so that every read reaches the leaves.
log_must zfs create -o recordsize=128k -o compression=off -o atime=off \
    -o primarycache=metadata "$TESTPOOL1/$TESTFS"
mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
log_must dd if=/dev/urandom of="$workdir/expected" bs=128k count=1
log_must cp "$workdir/expected" "$mntpnt/file"
sync_pool "$TESTPOOL1"
object=$(get_objnum "$mntpnt/file")

log_must zpool add "$TESTPOOL1" "${VDEV_FILES[0]}"
log_must set_tunable32 REMOVE_MAX_SEGMENT 32768
log_must zpool remove -w "$TESTPOOL1" "${VDEV_FILES[1]}"
log_must set_tunable32 REMOVE_MAX_SEGMENT "$ORIG_REMOVE_MAX_SEGMENT"

# Locate the first file segment in the actual indirect mapping.
log_must eval "zdb -dddddd '$TESTPOOL1/$TESTFS' '$object' >'$workdir/block'"
dva=$(awk '$1 == "0" && $2 == "L0" { print $3 }' "$workdir/block")
[[ "$dva" == 0:*:20000 ]] ||
    log_fail "expected a 128K file block on the removed vdev: $dva"
offset=${dva#0:}
offset=${offset%:*}
typeset -i file_offset="16#$offset"
log_must eval "zdb -mmmm '$TESTPOOL1' >'$workdir/mapping'"
segment_size=0
while read -r src arrow dst rest; do
	[[ "$src" == '<0:'* && "$arrow" == '->' ]] || continue
	src=${src#'<0:'}
	src=${src%'>'}
	typeset -i start="16#${src%:*}"
	typeset -i size="16#${src#*:}"
	(( file_offset >= start && file_offset < start + size )) || continue
	[[ "$dst" == '<1:'* ]] ||
	    log_fail "file segment mapped to an unexpected vdev: $dst"
	dst=${dst#'<1:'}
	dst=${dst%'>'}
	typeset -i target_offset="16#${dst%:*}"
	(( target_offset += file_offset - start ))
	(( segment_size = size - (file_offset - start) ))
	break
done < "$workdir/mapping"
(( segment_size > 0 && segment_size < 131072 )) ||
    log_fail "file block was not split (first segment size=$segment_size)"
segment=$(printf "%x:%x:r" "$target_offset" "$segment_size")
log_must dd if="$workdir/expected" of="$workdir/expected-segment" \
    bs=512 count=$((segment_size / 512))

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace "$TESTPOOL1" "${VDEV_FILES[0]}" "$SPARE_VDEV_FILE"
log_must is_pool_resilvering "$TESTPOOL1"
# Drop the write before I/O; report failure at completion to retain its DTL.
log_must zinject -d "$SPARE_VDEV_FILE" -e noop -T write -f 100 "$TESTPOOL1"
log_must zinject -d "$SPARE_VDEV_FILE" -e io -T write -f 100 "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c all
log_must is_pool_replacing "$TESTPOOL1"

# Raw leaf reads cannot fall back to the other child or heal the target.
log_must eval "zdb -R '$TESTPOOL1' '${VDEV_FILES[0]}:$segment' \
    >'$workdir/original-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/original-segment"
log_must eval "zdb -R '$TESTPOOL1' '$SPARE_VDEV_FILE:$segment' \
    >'$workdir/target-segment'"
log_must test "$(wc -c < "$workdir/target-segment")" -eq "$segment_size"
log_mustnot cmp "$workdir/expected-segment" "$workdir/target-segment"

# A checksumless read through the replacing mirror would fall back to the
# stale target after the original's read error, then self-heal the original
# with the target's bytes. Keep the scan suspended so the target stays stale.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must stat "$mntpnt/file"
log_must zinject -d "${VDEV_FILES[0]}" -e io -T read -f 100 "$TESTPOOL1"
log_mustnot dd if="$mntpnt/file" of=/dev/null bs=128k count=1
log_must zinject -c all
log_must eval "zdb -R '$TESTPOOL1' '${VDEV_FILES[0]}:$segment' \
    >'$workdir/original-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/original-segment"

# After a full-block checksum match, split reconstruction must repair even an
# unreadable destination. A failed read must not prevent writing valid data.
log_must zinject -d "$SPARE_VDEV_FILE" -e io -T read -f 100 "$TESTPOOL1"
# Reopen requests another healing pass after the write faults are removed.
log_must zpool reopen "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
sync_pool "$TESTPOOL1"
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zpool wait -t replace "$TESTPOOL1"
read_errors=$(zinject | awk '/^ *[0-9]/ { print $NF }')
(( read_errors > 0 )) || log_fail "target read injection did not fire"
log_must zinject -c all
log_mustnot is_pool_replacing "$TESTPOOL1"

log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$mntpnt/file"
log_must check_pool_status "$TESTPOOL1" "scan" \
    "resilvered .*with 0 errors"
log_must check_pool_status "$TESTPOOL1" "errors" "No known data errors"
log_must zdb -cdui "$TESTPOOL1/$TESTFS"

# The DTL is coarse: a missing target can hold the only valid copy of a segment.
# Reuse the former source without truncation, then establish the actual good
# target/bad source asymmetry with raw reads before exercising reconstruction.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace "$TESTPOOL1" "$SPARE_VDEV_FILE" "${VDEV_FILES[0]}"
log_must zinject -d "${VDEV_FILES[0]}" -e io -T write -f 100 "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c all
log_must is_pool_replacing "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

# A read error on the DTL-clean source must not fail a read which the
# DTL-dirty target's valid copy reconstructs, Direct I/O or not.
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
# Cache the file's metadata, which the DTL-dirty target may be missing.
log_must stat "$mntpnt/file"
log_must zinject -d "$SPARE_VDEV_FILE" -e io -T read -f 100 "$TESTPOOL1"
dio_reads=$(kstat_pool "$TESTPOOL1" iostats.direct_read_count)
log_must stride_dd -i "$mntpnt/file" -o "$workdir/direct" -b 131072 -c 1 -d
(( $(kstat_pool "$TESTPOOL1" iostats.direct_read_count) > dio_reads )) ||
    log_fail "the read did not use Direct I/O"
log_must cmp "$workdir/expected" "$workdir/direct"
log_must cmp "$workdir/expected" "$mntpnt/file"
read_errors=$(zinject | awk '/^ *[0-9]/ { print $NF }')
(( read_errors > 0 )) || log_fail "source read injection did not fire"
log_must zinject -c all
log_must zpool export "$TESTPOOL1"

# Leaf file offsets include the 4 MiB label/boot reservation.
log_must dd if=/dev/zero of="$SPARE_VDEV_FILE" bs=512 count=1 \
    seek=$(((target_offset + 4194304) / 512)) conv=notrunc
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must eval "zdb -R '$TESTPOOL1' '$SPARE_VDEV_FILE:$segment' \
    >'$workdir/original-segment'"
log_mustnot cmp "$workdir/expected-segment" "$workdir/original-segment"
log_must eval "zdb -R '$TESTPOOL1' '${VDEV_FILES[0]}:$segment' \
    >'$workdir/target-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/target-segment"

# An ordinary split read must not copy the bad source onto the good target.
# With the scan suspended, only the read's reconstruction can repair the source.
log_must cmp "$workdir/expected" "$mntpnt/file"
log_must eval "zdb -R '$TESTPOOL1' '${VDEV_FILES[0]}:$segment' \
    >'$workdir/target-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/target-segment"
log_must eval "zdb -R '$TESTPOOL1' '$SPARE_VDEV_FILE:$segment' \
    >'$workdir/original-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/original-segment"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t replace "$TESTPOOL1"
log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$mntpnt/file"
log_must zdb -cdui "$TESTPOOL1/$TESTFS"

for topology in replacing spare; do
	log_note "Split recovery through a nested $topology"
	log_must truncate -s 512M "${VDEV_FILES[2]}"
	log_must zpool attach -w "$TESTPOOL1" \
	    "${VDEV_FILES[0]}" "${VDEV_FILES[2]}"
	if [[ "$topology" == spare ]]; then
		log_must zpool add "$TESTPOOL1" spare "$SPARE_VDEV_FILE"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace "$TESTPOOL1" \
	    "${VDEV_FILES[0]}" "$SPARE_VDEV_FILE"
	log_must zinject -d "$SPARE_VDEV_FILE" -e io -T write -f 100 "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver "$TESTPOOL1"
	log_must zinject -c all
	log_must eval "zpool status '$TESTPOOL1' | grep -q '$topology-[0-9]'"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool export "$TESTPOOL1"

	# No complete combination is valid. Each leaf must retain its own bytes.
	for leaf in "${VDEV_FILES[0]}" "${VDEV_FILES[2]}" "$SPARE_VDEV_FILE"; do
		log_must dd if=/dev/zero of="$leaf" bs=512 count=1 \
		    seek=$(((target_offset + 4194304) / 512)) conv=notrunc
	done
	# Give the target different bad bytes, leaving distinct candidates.
	# Identical bad bytes would hide a checksumless copy between children.
	log_must dd if=/dev/urandom of="$workdir/bad-sector" bs=512 count=1
	log_must dd if="$workdir/bad-sector" of="$SPARE_VDEV_FILE" \
	    bs=512 count=1 seek=$(((target_offset + 4194304) / 512)) conv=notrunc
	log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
	for leaf in "${VDEV_FILES[0]}" "${VDEV_FILES[2]}" "$SPARE_VDEV_FILE"; do
		log_must eval "zdb -R '$TESTPOOL1' '$leaf:$segment' \
		    >'$workdir/before-${leaf##*/}'"
	done
	log_mustnot dd if="$mntpnt/file" of=/dev/null bs=128k count=1
	for leaf in "${VDEV_FILES[0]}" "${VDEV_FILES[2]}" "$SPARE_VDEV_FILE"; do
		log_must eval "zdb -R '$TESTPOOL1' '$leaf:$segment' \
		    >'$workdir/after-${leaf##*/}'"
		log_must cmp "$workdir/before-${leaf##*/}" "$workdir/after-${leaf##*/}"
	done

	# Supply the only valid segment on the DTL-dirty nested target.
	log_must zpool export "$TESTPOOL1"
	log_must dd if="$workdir/expected-segment" of="$SPARE_VDEV_FILE" \
	    bs=512 seek=$(((target_offset + 4194304) / 512)) conv=notrunc
	log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver "$TESTPOOL1"
	if [[ "$topology" == spare ]]; then
		# A healed spare stays attached until the original is removed.
		log_must zpool detach "$TESTPOOL1" "${VDEV_FILES[0]}"
	else
		log_must zpool wait -t replace "$TESTPOOL1"
	fi
	log_must zpool detach "$TESTPOOL1" "${VDEV_FILES[2]}"
	log_must zpool replace -w "$TESTPOOL1" \
	    "$SPARE_VDEV_FILE" "${VDEV_FILES[0]}"
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	log_must zdb -cdui "$TESTPOOL1/$TESTFS"
done

log_pass "Split indirect reconstruction preserved and validated repair copies"
