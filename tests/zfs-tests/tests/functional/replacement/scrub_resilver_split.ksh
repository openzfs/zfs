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
#	A DTL-limited scrub repairs split indirect blocks before detaching
#	the original replacement child.
#
# STRATEGY:
#	1. Remove a vdev with 128K file blocks using 32K removal segments.
#	2. Replace the remaining vdev, silently dropping writes to the target.
#	   A checkpoint preserves its DTL when this initial resilver finishes.
#	3. Remove the injection and checkpoint, and verify that a mapped file
#	   segment is present on the original but missing from the target.
#	4. Scrub, wait for replacement completion, and verify the file's
#	   contents after exporting/importing the pool.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	set_tunable32 SCAN_SUSPEND_PROGRESS \
	    "$ORIG_SCAN_SUSPEND_PROGRESS" >/dev/null 2>&1
	set_tunable32 REMOVE_MAX_SEGMENT \
	    "$ORIG_REMOVE_MAX_SEGMENT" >/dev/null 2>&1
	zpool checkpoint -d -w "$TESTPOOL1" >/dev/null 2>&1
	destroy_pool "$TESTPOOL1"
	rm -f "${VDEV_FILES[0]}" "${VDEV_FILES[1]}" "$SPARE_VDEV_FILE"
	rm -rf "$workdir"
}

log_assert "DTL-limited scrubs repair incomplete split indirect replacements"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
workdir=$(mktemp -d "$TEST_BASE_DIR/scrub_resilver_split.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must zinject -c all
# Removal needs free space beyond the pool's minimum slop reservation.
log_must truncate -s 512M \
    "${VDEV_FILES[0]}" "${VDEV_FILES[1]}" "$SPARE_VDEV_FILE"
log_must zpool create -f -o feature@resilver_defer=disabled \
    "$TESTPOOL1" "${VDEV_FILES[1]}"
log_must zfs create -o recordsize=128k -o compression=off -o atime=off \
    "$TESTPOOL1/$TESTFS"
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
log_must zinject -d "$SPARE_VDEV_FILE" -e noop -T write -f 100 "$TESTPOOL1"
log_must zpool checkpoint "$TESTPOOL1"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver "$TESTPOOL1"
log_must zinject -c all
log_must zpool checkpoint -d -w "$TESTPOOL1"
log_must is_pool_replacing "$TESTPOOL1"

# Raw leaf reads cannot fall back to the other child or heal the target.
log_must eval "zdb -R '$TESTPOOL1' '${VDEV_FILES[0]}:$segment' \
    >'$workdir/original-segment'"
log_must cmp "$workdir/expected-segment" "$workdir/original-segment"
log_must eval "zdb -R '$TESTPOOL1' '$SPARE_VDEV_FILE:$segment' \
    >'$workdir/target-segment'"
log_must test "$(wc -c < "$workdir/target-segment")" -eq "$segment_size"
log_mustnot cmp "$workdir/expected-segment" "$workdir/target-segment"

log_must zpool scrub "$TESTPOOL1"
log_must zpool wait -t scrub "$TESTPOOL1"
log_must zpool wait -t replace "$TESTPOOL1"
log_mustnot is_pool_replacing "$TESTPOOL1"

log_must zpool export "$TESTPOOL1"
log_must zpool import -d "$TEST_BASE_DIR" "$TESTPOOL1"
log_must cmp "$workdir/expected" "$mntpnt/file"
log_must check_pool_status "$TESTPOOL1" "scan" \
    "scrub repaired [1-9].*with 0 errors"
log_must check_pool_status "$TESTPOOL1" "errors" "No known data errors"
log_must zdb -cdui "$TESTPOOL1/$TESTFS"

log_pass "DTL-limited scrubs repair incomplete split indirect replacements"
