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
#	Split indirect reconstruction tries the copies which are not missing
#	the block before sampling combinations, so readable stale copies of
#	many splits cannot hide an intact source.
#
# STRATEGY:
#	1. Remove a vdev with 1M file blocks using 8K removal segments, so
#	   that the block has more combinations than the attempt limit.
#	2. Add a stale copy beside the intact one, with healing suspended.
#	3. Damage the intact copy's first segment. With no valid combination,
#	   a read must fail rather than enumerate indefinitely.
#	4. Restore it and heal. Retire the intact copy and verify a cold read
#	   of the healed one.
#	5. Repeat with a hot spare whose original is offline, so the first
#	   copy with data is the stale spare and the intact copy comes later.
#

verify_runnable "global"

function cleanup
{
	# Destroy before restoring progress so cleanup cannot finish repair.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS \
	    "$ORIG_SCAN_SUSPEND_PROGRESS" >/dev/null 2>&1
	set_tunable32 REMOVE_MAX_SEGMENT \
	    "$ORIG_REMOVE_MAX_SEGMENT" >/dev/null 2>&1
	rm -rf "$workdir"
}

# Leaf file offset of an allocated offset, past the 4 MiB front reservation.
function leaf_sector
{
	echo $(( ($1 + 4194304) / 512 ))
}

# Create a pool whose single 1M file block is split across many mappings on
# disk-0, and set disk_offset and segment_size to its first segment.
function create_split_pool
{
	# Stale copies must not keep data from an earlier case.
	log_must rm -f "$workdir"/disk-*
	log_must truncate -s 512M "$workdir"/disk-{0,1,2,3}
	log_must zpool create -f \
	    -o feature@resilver_defer=disabled "$TESTPOOL1" "$workdir/disk-1"
	log_must zfs create -o recordsize=1m -o compression=off -o atime=off \
	    -o primarycache=metadata "$TESTPOOL1/$TESTFS"
	mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
	log_must cp "$workdir/expected" "$mntpnt/file"
	sync_pool "$TESTPOOL1"
	object=$(get_objnum "$mntpnt/file")
	log_must zpool add "$TESTPOOL1" "$workdir/disk-0"
	log_must set_tunable32 REMOVE_MAX_SEGMENT 8192
	log_must zpool remove -w "$TESTPOOL1" "$workdir/disk-1"
	log_must set_tunable32 REMOVE_MAX_SEGMENT "$ORIG_REMOVE_MAX_SEGMENT"

	log_must eval "zdb -dddddd '$TESTPOOL1/$TESTFS' '$object' \
	    >'$workdir/block'"
	typeset dva
	dva=$(awk '$1 == "0" && $2 == "L0" { print $3 }' "$workdir/block")
	[[ "$dva" == 0:*:100000 ]] ||
	    log_fail "expected a 1M file block on the removed vdev: $dva"
	typeset offset=${dva#0:}
	typeset -i file_offset="16#${offset%:*}" splits=0 start size
	typeset src arrow dst rest
	segment_size=0
	log_must eval "zdb -mmmm '$TESTPOOL1' >'$workdir/mapping'"
	while read -r src arrow dst rest; do
		[[ "$src" == '<0:'* && "$arrow" == '->' ]] || continue
		src=${src#'<0:'}
		src=${src%'>'}
		start="16#${src%:*}"
		size="16#${src#*:}"
		(( file_offset < start + size &&
		    start < file_offset + 1048576 )) || continue
		if (( file_offset >= start )); then
			dst=${dst#'<1:'}
			dst=${dst%'>'}
			disk_offset=$(( 16#${dst%:*} + file_offset - start ))
			segment_size=$(( size - (file_offset - start) ))
		fi
		(( splits += 1 ))
	done < "$workdir/mapping"
	# 64 splits with two distinct copies each exceed 2^64 combinations.
	(( splits > 64 )) || log_fail "file block has only $splits segments"
	(( segment_size > 0 )) || log_fail "first file segment not mapped"
	log_must dd if="$workdir/expected" of="$workdir/expected-segment" \
	    bs=512 count=$((segment_size / 512))
}

# Without a valid combination, the read must fail within the attempt limit.
# A regression that enumerates every combination does not return.
function read_fails_bounded
{
	typeset -i rc
	log_must zpool export "$TESTPOOL1"
	log_must dd if=/dev/zero of="$1" bs=512 count=$((segment_size / 512)) \
	    seek=$(leaf_sector "$disk_offset") conv=notrunc
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	timeout 120 dd if="$mntpnt/file" of=/dev/null bs=1M count=1
	rc=$?
	(( rc == 1 )) || log_fail "unrecoverable read returned $rc"
	log_must zpool export "$TESTPOOL1"
	log_must dd if="$workdir/expected-segment" of="$1" bs=512 \
	    seek=$(leaf_sector "$disk_offset") conv=notrunc
	log_must zpool import -d "$workdir" "$TESTPOOL1"
}

# Heal with healing resumed, then keep only the healed leaf and read it cold.
function verify_healed
{
	typeset healed=$1 leaf
	shift
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must zpool wait -t resilver "$TESTPOOL1"
	for leaf in "$@"; do
		log_must zpool detach "$TESTPOOL1" "$leaf"
	done
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must eval "zpool status '$TESTPOOL1' | grep -q '${healed##*/}'"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	log_must zdb -cdui "$TESTPOOL1/$TESTFS"
	destroy_pool "$TESTPOOL1"
}

log_assert "Split reconstruction tries intact copies before sampling"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
workdir=$(mktemp -d "$TEST_BASE_DIR/indirect_reconstruct.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=1

log_note "Stale mirror child"
# The attached child is missing the block but reads as zeros.
create_split_pool
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-2"
read_fails_bounded "$workdir/disk-0"
verify_healed "$workdir/disk-2" "$workdir/disk-0"

log_note "Stale spare ahead of the intact copy"
create_split_pool
log_must zpool attach -w "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-2"
log_must zpool add "$TESTPOOL1" spare "$workdir/disk-3"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace "$TESTPOOL1" "$workdir/disk-0" "$workdir/disk-3"
log_must zpool offline "$TESTPOOL1" "$workdir/disk-0"
# A healed spare stays attached until the original is removed.
verify_healed "$workdir/disk-3" "$workdir/disk-0" "$workdir/disk-2"

log_pass "Split reconstruction tried intact copies before sampling"
