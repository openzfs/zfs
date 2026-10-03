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
# Multiple bookmarks from one dataset can be destroyed in one command, and
# -r applies the same bookmark names to descendant datasets.
#
# STRATEGY:
# - Verify the legacy single-bookmark compatibility boundary.
# - Verify batching, recursion, deduplication, and missing-name handling.
# - Verify case-sensitive names stay distinct and aliases are deduplicated.
# - Verify filesystems and volumes.
# - Verify impossible overlong targets are omitted from new-style requests.
# - Verify a large recursive target product is accepted.
# - Verify unsupported options, malformed specifications, and cross-dataset
#   specifications destroy nothing.
#

verify_runnable "both"

typeset ROOT="$TESTPOOL/${TESTFS1}_bookmark_destroy"
typeset CHILD="$ROOT/child"
typeset GRANDCHILD="$CHILD/grandchild"
typeset INSENSITIVE="$ROOT/insensitive"
typeset LATE="$ROOT/late"
typeset VOL="$ROOT/vol"
typeset OUTSIDE="${ROOT}_outside"
typeset REDACT_SOURCE="$ROOT/redact_source"
typeset REDACT_CLONE="$ROOT/redact_clone"
typeset OVERFLOW_ROOT="$TESTPOOL2/${TESTFS1}_bookmark_destroy_overflow"
typeset OVERFLOW_VDEV="$TEST_BASE_DIR/zfs_destroy_bookmarks_overflow.$$"
typeset REDACT_ERROR="$TEST_BASE_DIR/zfs_destroy_bookmarks_redact_error.$$"
typeset REDACT_SEND_PID=
typeset -a DESCENDANTS=("$CHILD" "$GRANDCHILD" "$INSENSITIVE" "$VOL")
typeset -r ZFS_MAX_DATASET_NAME_LEN=256
typeset -r BATCH_LENGTH_NAME=length_batch
typeset -r RECURSIVE_LENGTH_NAME=length_recursive
typeset -r SYNC_TASK_SAFE_TARGETS=43690
typeset -r OVERFLOW_DATASET_COUNT=4
typeset -r OVERFLOW_NAME_COUNT=$((SYNC_TASK_SAFE_TARGETS / \
	OVERFLOW_DATASET_COUNT + 1))
typeset -r IMPOSSIBLE_NAME=$(gen_dataset_name \
	$((ZFS_MAX_DATASET_NAME_LEN - ${#ROOT} - 1)) x)
typeset -r OVERLONG_CHILD="$ROOT/$(gen_dataset_name \
	$((ZFS_MAX_DATASET_NAME_LEN - ${#ROOT} - \
	${#RECURSIVE_LENGTH_NAME} - 2)) x)"

function cleanup
{
	if [[ -n "$REDACT_SEND_PID" ]]; then
		kill "$REDACT_SEND_PID" 2>/dev/null
		wait "$REDACT_SEND_PID" 2>/dev/null
		REDACT_SEND_PID=
	fi
	datasetexists "$ROOT" && destroy_dataset "$ROOT" "-r"
	datasetexists "$OUTSIDE" && destroy_dataset "$OUTSIDE" "-r"
	poolexists "$TESTPOOL2" && destroy_pool "$TESTPOOL2"
	[[ -e "$OVERFLOW_VDEV" ]] && log_must rm -f "$OVERFLOW_VDEV"
	[[ -e "$REDACT_ERROR" ]] && log_must rm -f "$REDACT_ERROR"
}

function bookmark_must_exist
{
	bkmarkexists "$1" || log_fail "Bookmark $1 does not exist"
}

function bookmark_must_not_exist
{
	bkmarkexists "$1" && log_fail "Bookmark $1 exists"
	return 0
}

log_assert "zfs destroy handles multiple and recursive bookmarks"
log_onexit cleanup

log_must zfs create "$ROOT"
log_must zfs create "$CHILD"
log_must zfs create "$GRANDCHILD"
log_must zfs create -o casesensitivity=insensitive "$INSENSITIVE"
log_must zfs create -V 16M -o volmode=none "$VOL"
log_must zfs create "$OUTSIDE"
log_must zfs snapshot -r "$ROOT@source"
log_must zfs snapshot "$OUTSIDE@source"

# A non-recursive single bookmark retains the legacy behavior, including an
# error for a missing bookmark.  A list or -r selects the new behavior, where
# missing bookmarks are successful no-ops.
log_must zfs bookmark "$ROOT@source" "$ROOT#legacy"
log_must zfs destroy "$ROOT#legacy"
bookmark_must_not_exist "$ROOT#legacy"
log_mustnot zfs destroy "$ROOT#missing"
log_must zfs destroy "$ROOT#missing1,missing2"
log_must zfs destroy -r "$ROOT#missing"

# Case-equivalent names are distinct on a sensitive dataset, but are submitted
# only once per insensitive dataset.
for name in mixed MiXeD; do
	log_must zfs bookmark "$ROOT@source" "$ROOT#$name"
done
log_must zfs bookmark "$INSENSITIVE@source" "$INSENSITIVE#MiXeD"
log_must zfs destroy -r "$ROOT#mixed,MiXeD"
for name in mixed MiXeD; do
	bookmark_must_not_exist "$ROOT#$name"
done
bookmark_must_not_exist "$INSENSITIVE#MiXeD"

# An alias and an unrelated name remain independent on an insensitive dataset.
for name in MiXeD FOO; do
	log_must zfs bookmark "$INSENSITIVE@source" "$INSENSITIVE#$name"
done
log_must zfs destroy "$INSENSITIVE#mixed,FOO"
bookmark_must_not_exist "$INSENSITIVE#MiXeD"
bookmark_must_not_exist "$INSENSITIVE#FOO"

# A non-recursive batch only affects the named dataset.  Duplicate names are
# submitted once.  Recursive destroy then removes the remaining descendant
# bookmarks, skips the late descendant, and leaves a prefix sibling alone.
for name in one two keep; do
	log_must zfs bookmark -r "$ROOT@source" "$ROOT#$name"
	log_must zfs bookmark "$OUTSIDE@source" "$OUTSIDE#$name"
done
log_must zfs destroy "$ROOT#one,two,one"
for name in one two; do
	bookmark_must_not_exist "$ROOT#$name"
	for ds in "${DESCENDANTS[@]}"; do
		bookmark_must_exist "$ds#$name"
	done
done

log_must zfs create "$LATE"
log_must zfs destroy -r "$ROOT#one,two,one,missing"
for ds in "$ROOT" "${DESCENDANTS[@]}" "$LATE"; do
	for name in one two; do
		bookmark_must_not_exist "$ds#$name"
	done
	if [[ $ds == "$LATE" ]]; then
		bookmark_must_not_exist "$ds#keep"
	else
		bookmark_must_exist "$ds#keep"
	fi
done
for name in one two keep; do
	bookmark_must_exist "$OUTSIDE#$name"
done

# A recursive command with one bookmark also uses the new path.
log_must zfs bookmark -r "$ROOT@source" "$ROOT#single_recursive"
log_must zfs destroy -r "$ROOT#single_recursive"
for ds in "$ROOT" "${DESCENDANTS[@]}" "$LATE"; do
	bookmark_must_not_exist "$ds#single_recursive"
done

# Volumes accept both a list and -r; recursion has no additional effect.
for name in vol1 vol2 vol3; do
	log_must zfs bookmark "$VOL@source" "$VOL#$name"
done
log_must zfs destroy "$VOL#vol1,vol2,vol1,missing"
bookmark_must_not_exist "$VOL#vol1"
bookmark_must_not_exist "$VOL#vol2"
log_must zfs destroy -r "$VOL#vol3"
bookmark_must_not_exist "$VOL#vol3"

# Debug builds can simulate a newer userland with an older kernel before the
# ioctl is issued.  Confirm new-style requests fail without falling back to the
# legacy ioctl.  Production builds ignore ZFS_IOC_TEST and skip this subtest.
typeset legacy_destroy_ioc=69
typeset destroy_v2_ioc=91
if is_linux; then
	legacy_destroy_ioc=0x5a45
	destroy_v2_ioc=0x5a5b
fi
log_must zfs bookmark "$ROOT@source" "$ROOT#ioc_probe"
if env ZFS_IOC_TEST="$legacy_destroy_ioc:1029" \
    lzc_destroy_bookmarks "$ROOT#ioc_probe" >/dev/null 2>&1; then
	bookmark_must_not_exist "$ROOT#ioc_probe"
else
	bookmark_must_exist "$ROOT#ioc_probe"
	log_must zfs destroy "$ROOT#ioc_probe"
	log_must zfs bookmark "$ROOT@source" "$ROOT#no_fallback"
	typeset ioc_error="$TEST_BASE_DIR/zfs_destroy_bookmarks_ioc.$$"
	log_mustnot eval "env ZFS_IOC_TEST=$destroy_v2_ioc:1029 zfs destroy \
	    $ROOT#no_fallback,missing >$ioc_error 2>&1"
	log_must grep -q "loaded zfs module does not support this operation" \
	    "$ioc_error"
	log_must rm -f "$ioc_error"
	bookmark_must_exist "$ROOT#no_fallback"
	log_must zfs destroy "$ROOT#no_fallback"
fi

# New-style batch and recursive requests omit each qualified bookmark name that
# reaches the full-name limit and therefore cannot exist.
(( ${#ROOT} + 1 + ${#IMPOSSIBLE_NAME} == ZFS_MAX_DATASET_NAME_LEN )) ||
	log_fail "Unexpected impossible bookmark name length"

log_must zfs bookmark "$ROOT@source" "$ROOT#$BATCH_LENGTH_NAME"
log_must zfs destroy "$ROOT#$BATCH_LENGTH_NAME,$IMPOSSIBLE_NAME"
bookmark_must_not_exist "$ROOT#$BATCH_LENGTH_NAME"
log_must zfs destroy "$ROOT#$IMPOSSIBLE_NAME,$IMPOSSIBLE_NAME"

(( ${#OVERLONG_CHILD} + 1 + ${#RECURSIVE_LENGTH_NAME} == \
	ZFS_MAX_DATASET_NAME_LEN )) ||
	log_fail "Unexpected recursive bookmark name length"
log_must zfs create "$OVERLONG_CHILD"
log_must zfs bookmark "$ROOT@source" "$ROOT#$RECURSIVE_LENGTH_NAME"
log_must zfs destroy -r "$ROOT#$RECURSIVE_LENGTH_NAME"
bookmark_must_not_exist "$ROOT#$RECURSIVE_LENGTH_NAME"

# Recursive expansion can submit more targets than the sync-task MOS
# reservation can represent in a signed int.  Missing bookmarks are sufficient
# because they are submitted to the same kernel path and are successful no-ops.
log_must truncate -s 4G "$OVERFLOW_VDEV"
log_must zpool create "$TESTPOOL2" "$OVERFLOW_VDEV"
log_must zfs create "$OVERFLOW_ROOT"
for child in one two three; do
	log_must zfs create "$OVERFLOW_ROOT/$child"
done
typeset overflow_names
overflow_names=$(awk -v count="$OVERFLOW_NAME_COUNT" 'BEGIN {
	for (i = 0; i < count; i++)
		printf "%sb%d", (i == 0 ? "" : ","), i
}')
(( OVERFLOW_DATASET_COUNT * OVERFLOW_NAME_COUNT > \
	SYNC_TASK_SAFE_TARGETS )) || log_fail "Target count is below boundary"
log_must zfs destroy -r "$OVERFLOW_ROOT#$overflow_names"
log_must zfs destroy -r "$OVERFLOW_ROOT"
log_must destroy_pool "$TESTPOOL2"
log_must rm -f "$OVERFLOW_VDEV"

# Unsupported, invalid, and cross-dataset forms fail before destroying any
# bookmark.
for name in invalid1 invalid2; do
	log_must zfs bookmark "$ROOT@source" "$ROOT#$name"
done
log_must zfs bookmark "$OUTSIDE@source" "$OUTSIDE#outside"
for ds in "$ROOT" "$CHILD"; do
	log_must zfs bookmark "$ds@source" "$ds#reject_R"
done
log_mustnot zfs destroy -R "$ROOT#reject_R"
bookmark_must_exist "$ROOT#reject_R"
bookmark_must_exist "$CHILD#reject_R"
log_mustnot zfs destroy "$ROOT#invalid1" "$OUTSIDE#outside"
log_mustnot zfs destroy "$ROOT#invalid1,$OUTSIDE#outside"
log_mustnot zfs destroy "$ROOT#invalid1%invalid2"
log_mustnot zfs destroy "$ROOT#invalid1,,invalid2"
log_mustnot zfs destroy "$ROOT#invalid1,."
log_mustnot zfs destroy "$ROOT#invalid2,.."
for name in invalid1 invalid2; do
	bookmark_must_exist "$ROOT#$name"
done
bookmark_must_exist "$OUTSIDE#outside"

# A busy redaction bookmark identifies the exact failing target, and the
# otherwise valid bookmark remains because batch destruction is atomic.
log_must zfs create "$REDACT_SOURCE"
typeset redact_mount
redact_mount=$(get_prop mountpoint "$REDACT_SOURCE")
log_must dd if=/dev/urandom of="$redact_mount/data" bs=1M count=8
log_must zfs snapshot "$REDACT_SOURCE@source"
log_must zfs clone "$REDACT_SOURCE@source" "$REDACT_CLONE"
log_must zfs snapshot "$REDACT_CLONE@source"
log_must zfs redact "$REDACT_SOURCE@source" busy \
	"$REDACT_CLONE@source"
log_must zfs bookmark "$REDACT_SOURCE@source" "$REDACT_SOURCE#free"

zfs send --redact busy "$REDACT_SOURCE@source" |&
REDACT_SEND_PID=$!
log_must dd bs=1 count=1 <&p >/dev/null 2>&1

log_mustnot eval "zfs destroy '$REDACT_SOURCE#free,busy' \
	>'$REDACT_ERROR' 2>&1"
log_must grep -Fxq \
	"cannot destroy bookmark $REDACT_SOURCE#busy: dataset is busy" \
	"$REDACT_ERROR"
log_must test "$(wc -l < "$REDACT_ERROR")" -eq 1
bookmark_must_exist "$REDACT_SOURCE#busy"
bookmark_must_exist "$REDACT_SOURCE#free"

kill "$REDACT_SEND_PID" 2>/dev/null
wait "$REDACT_SEND_PID" 2>/dev/null
REDACT_SEND_PID=
log_must zfs destroy "$REDACT_SOURCE#busy,free"
log_must rm -f "$REDACT_ERROR"

log_pass "zfs destroy handles multiple and recursive bookmarks"
