#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Batched snapshot and bookmark listing honors its tunable bounds and handles
# projected objects across operational dataset states.
#
# STRATEGY:
# 1. Verify the tunable bounds, then isolate the minimum time budget from the
#    batch-size limit and exercise cursor progress at the minimum batch size.
# 2. Verify JSON preserves hidden creation-TXG metadata and integer fields.
# 3. Verify bookmark projection after its source snapshot has been destroyed.
# 4. Verify snapshots remain listable after an encrypted dataset's key unloads.
# 5. Verify recursive mixed-type output combines simple, projected snapshot,
#    and projected bookmark handles without omissions, duplicates, or changed
#    ordering.
# 6. Verify a held, deferred-destroy snapshot remains projected with userrefs.
# 7. Verify listsnapshots=on implicitly includes projected snapshots.
# 8. Verify a combined snapshot and bookmark list when only a bookmark exists.
# 9. Verify nonrecursive combined-type listing stays on each explicitly named
#     dataset and does not broaden mixed-type lists.
#

verify_runnable "global"
set -o pipefail

DATASET="$TESTPOOL/$TESTFS/projected_list"
ENCRYPTED_DATASET="$TESTPOOL/$TESTFS/projected_list_encrypted"
MIXED_DATASET="$TESTPOOL/$TESTFS/projected_list_mixed"
MIXED_CHILD="$MIXED_DATASET/child"
MIXED_VOLUME="$MIXED_DATASET/vol"
DATASET_MOUNT="$TESTDIR/projected_list"
BATCH_OUTPUT="$TEST_BASE_DIR/projected_list_batch.$$"
LEGACY_OUTPUT="$TEST_BASE_DIR/projected_list_legacy.$$"
EXPECTED_OUTPUT="$TEST_BASE_DIR/projected_list_expected.$$"
MARKER="$TEST_BASE_DIR/projected_list_marker.$$"
COLUMNS="createtxg,creation,guid,name,type,userrefs"
HOLD_TAG="projected-list"
DEFERRED_HOLD_TAG="projected-list-deferred"
ENCRYPTED_HOLD_TAG="projected-list-encrypted"
saved_listsnapshots=""

function cleanup
{
	zfs release "$HOLD_TAG" "$DATASET@after_append" >/dev/null 2>&1
	zfs release "$DEFERRED_HOLD_TAG" "$DATASET@deferred" \
	    >/dev/null 2>&1
	zfs release "$ENCRYPTED_HOLD_TAG" "$ENCRYPTED_DATASET@second" \
	    >/dev/null 2>&1
	[[ -n "$saved_listsnapshots" ]] && zpool set \
	    listsnapshots="$saved_listsnapshots" "$TESTPOOL" >/dev/null 2>&1
	rm -f "$BATCH_OUTPUT" "$LEGACY_OUTPUT" "$EXPECTED_OUTPUT" "$MARKER"
	datasetexists "$MIXED_DATASET" && zfs destroy -r "$MIXED_DATASET"
	datasetexists "$ENCRYPTED_DATASET" && \
	    zfs destroy -r "$ENCRYPTED_DATASET"
	datasetexists "$DATASET" && zfs destroy -r "$DATASET"
	log_must restore_tunable SNAPSHOT_LIST_BATCH_TIME_US
	log_must restore_tunable SNAPSHOT_LIST_BATCH_SIZE
}

function find_shim
{
	typeset helper helper_dir candidate

	helper=$(readlink -f "$(command -v snapshot_list_test)")
	helper_dir=${helper%/*}
	for candidate in \
	    "$helper_dir/.libs/libsnapshot_list_test_shim.so" \
	    "$helper_dir/libsnapshot_list_test_shim.so" \
	    "$STF_SUITE/bin/libsnapshot_list_test_shim.so"; do
		[[ -f "$candidate" ]] && print -- "$candidate" && return 0
	done
	return 1
}

function compare_projected_json_int
{
	typeset dataset="$1"
	typeset object_types="$2"
	typeset preload="$SHIM"
	typeset -i batch_calls

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -j --json-int -t '$object_types' -d 1 -o '$COLUMNS' " \
	    "'$dataset' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	batch_calls=$(wc -l < "$MARKER")
	(( batch_calls > 1 )) || log_fail \
	    "one-microsecond time budget did not split snapshot iteration"
	log_must eval "zfs list -j --json-int -t '$object_types' -d 1 " \
	    "-s available -o '$COLUMNS' '$dataset' > '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"
	log_must rm -f "$MARKER"
}

function compare_locked_encrypted
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot -o '$COLUMNS' " \
	    "'$ENCRYPTED_DATASET' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	log_must eval "zfs list -H -p -t snapshot " \
	    "-o '$COLUMNS,available' '$ENCRYPTED_DATASET' | cut -f1-6 " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"
	log_must rm -f "$MARKER"
}

function compare_recursive_hybrid
{
	typeset object_types="filesystem,volume,snapshot,bookmark"
	typeset columns="name,createtxg,guid"
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -r -t '$object_types' -o '$columns' " \
	    "'$MIXED_DATASET' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	log_must eval "zfs list -H -p -r -t '$object_types' " \
	    "-o '$columns,available' '$MIXED_DATASET' | cut -f1-3 " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"
	log_must rm -f "$MARKER"
}

function verify_nonrecursive_mixed_scope
{
	typeset columns="name,createtxg,guid"
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot,bookmark -d 1 -o '$columns' " \
	    "'$MIXED_DATASET' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	printf "%s\n" "$MIXED_DATASET@root" "$MIXED_DATASET#root" | \
	    sort > "$EXPECTED_OUTPUT"
	log_must eval "cut -f1 '$BATCH_OUTPUT' | sort > '$LEGACY_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$LEGACY_OUTPUT"

	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 " \
	    "-o '$columns' '$MIXED_DATASET' '$MIXED_CHILD' " \
	    "> '$BATCH_OUTPUT'"
	printf "%s\n" "$MIXED_DATASET@root" "$MIXED_DATASET#root" \
	    "$MIXED_CHILD@child" "$MIXED_CHILD#child" | \
	    sort > "$EXPECTED_OUTPUT"
	log_must eval "cut -f1 '$BATCH_OUTPUT' | sort > '$LEGACY_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$LEGACY_OUTPUT"

	log_must eval "zfs list -H -p -t filesystem,snapshot,bookmark " \
	    "-o name '$MIXED_DATASET' > '$BATCH_OUTPUT'"
	printf "%s\n" "$MIXED_DATASET" > "$EXPECTED_OUTPUT"
	log_must diff "$EXPECTED_OUTPUT" "$BATCH_OUTPUT"
	log_must rm -f "$MARKER"
}

function compare_deferred_snapshot
{
	typeset preload="$SHIM"
	typeset userrefs
	typeset -i matches

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot -o '$COLUMNS' " \
	    "'$DATASET' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	log_must eval "zfs list -H -p -t snapshot " \
	    "-o '$COLUMNS,available' '$DATASET' | cut -f1-6 " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"

	matches=$(awk -F '\t' -v target="$DATASET@deferred" \
	    '$4 == target { count++ } END { print count + 0 }' "$BATCH_OUTPUT")
	(( matches == 1 )) || log_fail \
	    "deferred-destroy snapshot appeared $matches times; expected once"
	userrefs=$(awk -F '\t' -v target="$DATASET@deferred" \
	    '$4 == target { print $6 }' "$BATCH_OUTPUT")
	[[ "$userrefs" == "1" ]] || log_fail \
	    "deferred-destroy snapshot has userrefs=$userrefs; expected 1"
	log_must rm -f "$MARKER"
}

function compare_implicit_snapshots
{
	typeset columns="name,createtxg,guid"
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	saved_listsnapshots=$(get_pool_prop listsnapshots "$TESTPOOL")
	log_must zpool set listsnapshots=on "$TESTPOOL"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -r -o '$columns' '$MIXED_DATASET' " \
	    "> '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	log_must eval "zfs list -H -p -r -o '$columns,available' " \
	    "'$MIXED_DATASET' | cut -f1-3 > '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"

	printf "%s\n" "$MIXED_DATASET" "$MIXED_DATASET@root" \
	    "$MIXED_CHILD" "$MIXED_CHILD@child" "$MIXED_VOLUME" \
	    "$MIXED_VOLUME@volume" | sort > "$EXPECTED_OUTPUT"
	log_must eval "cut -f1 '$BATCH_OUTPUT' | sort > '$LEGACY_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$LEGACY_OUTPUT"
	log_must rm -f "$MARKER"
	log_must zpool set listsnapshots="$saved_listsnapshots" "$TESTPOOL"
	saved_listsnapshots=""
}

function compare_bookmark_only_mixed_type
{
	typeset columns="name,createtxg,guid"
	typeset preload="$SHIM"
	typeset actual
	typeset -i lines

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot,bookmark -d 1 -o '$columns' " \
	    "'$MIXED_CHILD' > '$BATCH_OUTPUT'"
	log_must grep -Fx count "$MARKER"
	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 " \
	    "-o '$columns,available' '$MIXED_CHILD' | cut -f1-3 " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$BATCH_OUTPUT"

	lines=$(wc -l < "$BATCH_OUTPUT")
	(( lines == 1 )) || log_fail \
	    "bookmark-only mixed list returned $lines entries; expected one"
	actual=$(cut -f1 "$BATCH_OUTPUT")
	[[ "$actual" == "$MIXED_CHILD#child" ]] || log_fail \
	    "bookmark-only mixed list returned $actual"
	log_must rm -f "$MARKER"
}

SHIM=$(find_shim) || log_unsupported "snapshot-list test shim not found"
log_onexit cleanup
log_assert "Projected listing matches legacy output across operational states."

log_must save_tunable SNAPSHOT_LIST_BATCH_SIZE
log_must save_tunable SNAPSHOT_LIST_BATCH_TIME_US
log_mustnot set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 0
log_mustnot set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 0
log_mustnot set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 4097
log_mustnot set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 100001
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 4096
log_must set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 1

log_must zfs create "$DATASET"
log_must file_write -o create -f "$DATASET_MOUNT/payload" \
    -b 131072 -c 8 -d R
log_must zfs snapshot "$DATASET@base"
log_must file_write -o append -f "$DATASET_MOUNT/payload" \
    -b 131072 -c 4 -d R
log_must zfs snapshot "$DATASET@after_append"
log_must rm "$DATASET_MOUNT/payload"
log_must zfs snapshot "$DATASET@after_remove"
log_must zfs hold "$HOLD_TAG" "$DATASET@after_append"
log_must zfs bookmark "$DATASET@base" "$DATASET#base"
log_must zfs bookmark "$DATASET@after_append" "$DATASET#after_append"
log_must zfs snapshot "$DATASET@bookmark_source"
log_must zfs bookmark "$DATASET@bookmark_source" \
    "$DATASET#source_destroyed"
log_must zfs destroy "$DATASET@bookmark_source"
snapexists "$DATASET@bookmark_source" && \
    log_fail "bookmark source snapshot still exists"

compare_projected_json_int "$DATASET" snapshot,bookmark
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1
log_must set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 100000

log_must zfs snapshot "$DATASET@deferred"
log_must zfs hold "$DEFERRED_HOLD_TAG" "$DATASET@deferred"
log_must zfs destroy -d "$DATASET@deferred"
defer_destroy=$(get_prop defer_destroy "$DATASET@deferred")
[[ "$defer_destroy" == "on" ]] || log_fail \
    "deferred snapshot has defer_destroy=$defer_destroy; expected on"
compare_deferred_snapshot
log_must zfs release "$DEFERRED_HOLD_TAG" "$DATASET@deferred"
snapexists "$DATASET@deferred" && \
    log_fail "deferred snapshot remained after its final hold was released"

log_must eval "echo 'projected-list-password' | zfs create " \
    "-o encryption=on -o keyformat=passphrase -o keylocation=prompt " \
    "-o mountpoint=none '$ENCRYPTED_DATASET'"
log_must zfs snapshot "$ENCRYPTED_DATASET@first"
log_must zfs snapshot "$ENCRYPTED_DATASET@second"
log_must zfs hold "$ENCRYPTED_HOLD_TAG" "$ENCRYPTED_DATASET@second"
log_must snapshot_list_test encrypted-snapshot-metadata "$ENCRYPTED_DATASET"
log_must zfs unload-key "$ENCRYPTED_DATASET"
keystatus=$(get_prop keystatus "$ENCRYPTED_DATASET")
[[ "$keystatus" == "unavailable" ]] || \
    log_fail "encrypted dataset key remains $keystatus after unload"
log_must snapshot_list_test encrypted-snapshot-metadata "$ENCRYPTED_DATASET"
compare_locked_encrypted

log_must zfs create -o mountpoint=none "$MIXED_DATASET"
log_must zfs create -o mountpoint=none "$MIXED_CHILD"
log_must zfs create -V 64M "$MIXED_VOLUME"
log_must zfs snapshot "$MIXED_DATASET@root"
log_must zfs snapshot "$MIXED_CHILD@child"
log_must zfs snapshot "$MIXED_VOLUME@volume"
log_must zfs bookmark "$MIXED_DATASET@root" "$MIXED_DATASET#root"
log_must zfs bookmark "$MIXED_CHILD@child" "$MIXED_CHILD#child"
log_must zfs bookmark "$MIXED_VOLUME@volume" "$MIXED_VOLUME#volume"
verify_nonrecursive_mixed_scope
compare_recursive_hybrid
compare_implicit_snapshots
log_must zfs destroy "$MIXED_CHILD@child"
snapexists "$MIXED_CHILD@child" && \
    log_fail "bookmark source snapshot still exists"
compare_bookmark_only_mixed_type

log_pass "Projected listing matches legacy output across operational states."
