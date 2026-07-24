#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Projected snapshot and bookmark listing preserves ordering while handling
# buffer growth, old kernels, interruption, and injected iterator errors.
#
# STRATEGY:
# 1. Exercise synthetic and kernel-reported ENOMEM destination-buffer growth.
# 2. Reject every batch ioctl with CMD_UNAVAIL, ENOTTY, or ENOTSUP and verify
#    automatic legacy fallback.
# 3. Verify creation-TXG filters survive automatic legacy fallback.
# 4. Reject a projected property and verify automatic legacy fallback.
# 5. Verify written and -o all bypass projected listing.
# 6. Exercise projected numeric values used only as sort keys.
# 7. Exercise the same error paths with all projected columns and name alone.
# 8. Verify callback values which collide with special ioctl errors are
#    returned unchanged by snapshot and bookmark iteration.
# 9. Treat ioctl ENOENT and ESRCH before or after results as normal end.
# 10. Inject EINTR and require libzfs to preserve it.
# 11. Require direct projected properties and fail closed on materialization
#     ENOMEM.
# 12. Reject an ioctl after one callback without fallback or callback replay.
# 13. Continue after a valid empty, non-EOF batch with an advancing cursor.
# 14. Verify filtered-out snapshots do not consume result batch slots.
# 15. Verify old-kernel fallback preserves mixed snapshots and bookmarks,
#     including a bookmark whose source snapshot has been destroyed.
# 16. Preserve bookmark iterator errors.
# 17. Reuse stale parent names with different dataset types and encryption
#     states, and require projected handles to describe the replacements.
# 18. Reject missing or invalid projected parent metadata with EPROTO.
#

verify_runnable "global"
set -o pipefail

DATASET="$TESTPOOL/$TESTFS/projected_stress"
STALE_DESTROY_DATASET="$DATASET/stale_destroy"
STALE_RENAME_DATASET="$DATASET/stale_rename"
STALE_RENAMED_DATASET="$DATASET/stale_renamed"
STALE_TYPE_DATASET="$DATASET/stale_type"
STALE_ENCRYPTED_DATASET="$DATASET/stale_encrypted"
BATCH_OUTPUT="$TEST_BASE_DIR/projected_stress_batch.$$"
INJECTED_OUTPUT="$TEST_BASE_DIR/projected_stress_injected.$$"
EXPECTED_OUTPUT="$TEST_BASE_DIR/projected_stress_expected.$$"
MARKER="$TEST_BASE_DIR/projected_stress_marker.$$"
KEY_FILE="$TEST_BASE_DIR/projected_stress_key.$$"
COLUMNS="createtxg,creation,guid,name,type,userrefs"
HOLD_TAG_ONE="projected-stress-one"
HOLD_TAG_TWO="projected-stress-two"

function cleanup
{
	if snapexists "$DATASET@z_middle"; then
		zfs release "$HOLD_TAG_ONE" "$DATASET@z_middle" \
		    >/dev/null 2>&1
		zfs release "$HOLD_TAG_TWO" "$DATASET@z_middle" \
		    >/dev/null 2>&1
	fi
	rm -f "$BATCH_OUTPUT" "$INJECTED_OUTPUT" "$EXPECTED_OUTPUT" \
	    "$MARKER" "$KEY_FILE"
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

function run_injected_list
{
	typeset mode="$1"
	typeset output="$2"
	typeset columns="${3:-$COLUMNS}"
	typeset expected="${4:-$BATCH_OUTPUT}"
	typeset sort_options="$5"
	typeset object_types="${6:-snapshot}"
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='$mode' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t '$object_types' -o '$columns' " \
	    "$sort_options " \
	    "'$DATASET' " \
	    "> '$output'"
	log_must grep -Fx "$mode" "$MARKER"
	log_must diff "$expected" "$output"
	log_must rm -f "$MARKER"
}

function verify_written_uses_legacy
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "zfs list -H -p -t snapshot -o name,written " \
	    "'$DATASET' > '$EXPECTED_OUTPUT'"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot -o name,written '$DATASET' " \
	    "> '$INJECTED_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$INJECTED_OUTPUT"
	[[ ! -e "$MARKER" ]] ||
	    log_fail "displaying written used projected listing"

	log_must eval "zfs list -H -p -t snapshot -o name -s written " \
	    "'$DATASET' > '$EXPECTED_OUTPUT'"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot -o name -s written '$DATASET' " \
	    "> '$INJECTED_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$INJECTED_OUTPUT"
	[[ ! -e "$MARKER" ]] ||
	    log_fail "sorting by written used projected listing"
}

function verify_all_uses_legacy
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must rm -f "$MARKER"
	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 -o all " \
	    "'$DATASET' > '$EXPECTED_OUTPUT'"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "zfs list -H -p -t snapshot,bookmark -d 1 -o all '$DATASET' " \
	    "> '$INJECTED_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$INJECTED_OUTPUT"
	[[ ! -e "$MARKER" ]] ||
	    log_fail "-o all used projected listing"
}

function run_injected_interrupt
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='eintr' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test interrupt '$DATASET'"
	log_must grep -Fx eintr "$MARKER"
	log_must rm -f "$MARKER"
}

function verify_direct_properties
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='direct_properties' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test direct-properties '$DATASET'"
	log_must grep -Fx direct_properties "$MARKER"
	! grep -q -Fx unexpected_property_nvlist "$MARKER" ||
	    log_fail "projected handle built per-property nvlists"
	log_must rm -f "$MARKER"
	log_must snapshot_list_test materialized-properties "$DATASET"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='direct_properties' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test refreshed-properties '$DATASET'"
	! grep -q -Fx unexpected_property_nvlist "$MARKER" ||
	    log_fail "refreshed handle retained projected properties"
	log_must rm -f "$MARKER"
}

function run_injected_handle_enomem
{
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='handle_enomem' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test handle-enomem '$DATASET'"
	log_must grep -Fx handle_enomem "$MARKER"
	log_must rm -f "$MARKER"
}

function run_injected_metadata_errors
{
	typeset mode preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	for mode in missing_dmu_type invalid_dmu_type missing_dds_flags \
	    invalid_dds_flags; do
		log_must rm -f "$MARKER"
		log_must eval "LD_PRELOAD='$preload' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MODE='$mode' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
		    "snapshot_list_test metadata-eproto '$DATASET'"
		log_must grep -Fx "$mode" "$MARKER"
	done
	log_must rm -f "$MARKER"
}

function run_injected_late_failure
{
	typeset preload="$SHIM"
	typeset -i lines

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_mustnot eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='enotsup_after_first' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test filter '$DATASET' 0 0 > '$INJECTED_OUTPUT'"
	log_must grep -Fx enotsup_after_first "$MARKER"
	lines=$(wc -l < "$INJECTED_OUTPUT")
	(( lines == 1 )) ||
	    log_fail "late batch failure delivered $lines snapshots; expected 1"
	log_must rm -f "$MARKER"
}

function run_injected_empty_batch
{
	typeset preload="$SHIM"
	typeset -i lines

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='empty_non_eof' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test filter '$DATASET' 0 0 " \
	    "> '$INJECTED_OUTPUT'"
	log_must grep -Fx empty_non_eof "$MARKER"
	# The shim consumes one result to obtain the kernel's opaque next cursor.
	lines=$(wc -l < "$INJECTED_OUTPUT")
	(( lines == 2 )) ||
	    log_fail "iteration after an empty batch delivered $lines snapshots; " \
	    "expected 2"
	log_must rm -f "$MARKER"
}

function run_injected_eof_after_first
{
	typeset mode
	typeset preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	log_must eval "snapshot_list_test filter '$DATASET' 0 0 " \
	    "> '$BATCH_OUTPUT'"
	log_must eval "head -n 1 '$BATCH_OUTPUT' > '$EXPECTED_OUTPUT'"
	[[ -s "$EXPECTED_OUTPUT" ]] ||
	    log_fail "unmodified iterator returned no first snapshot"

	for mode in enoent_after_first esrch_after_first; do
		log_must rm -f "$MARKER"
		log_must eval "LD_PRELOAD='$preload' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MODE='$mode' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
		    "snapshot_list_test filter '$DATASET' 0 0 " \
		    "> '$INJECTED_OUTPUT'"
		log_must grep -Fx "$mode" "$MARKER"
		log_must diff "$EXPECTED_OUTPUT" "$INJECTED_OUTPUT"
		log_must rm -f "$MARKER"
	done
}

function run_injected_bookmark_errors
{
	typeset error mode preload="$SHIM"
	typeset -i calls

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	for error in eio enoent esrch; do
		mode="bookmark_$error"
		log_must rm -f "$MARKER"
		log_must eval "LD_PRELOAD='$preload' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MODE='$mode' " \
		    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
		    "snapshot_list_test bookmark-error '$DATASET' '$error'"
		calls=$(grep -Fxc "$mode" "$MARKER")
		(( calls == 3 )) ||
		    log_fail "$mode was injected $calls times; expected 3"
	done
	log_must rm -f "$MARKER"
}

function verify_stale_bookmark_handles
{
	log_must zfs create -o mountpoint=none "$STALE_DESTROY_DATASET"
	log_must snapshot_list_test stale-bookmarks-destroy \
	    "$STALE_DESTROY_DATASET"
	datasetexists "$STALE_DESTROY_DATASET" &&
	    log_fail "stale-handle destroy left $STALE_DESTROY_DATASET"

	log_must zfs create -o mountpoint=none "$STALE_RENAME_DATASET"
	log_must snapshot_list_test stale-bookmarks-rename \
	    "$STALE_RENAME_DATASET" "$STALE_RENAMED_DATASET"
	datasetexists "$STALE_RENAME_DATASET" &&
	    log_fail "stale-handle rename left $STALE_RENAME_DATASET"
	datasetexists "$STALE_RENAMED_DATASET" ||
	    log_fail "stale-handle rename did not create $STALE_RENAMED_DATASET"
	log_must zfs destroy "$STALE_RENAMED_DATASET"
}

function verify_stale_snapshot_metadata
{
	log_must zfs create -o mountpoint=none "$STALE_TYPE_DATASET"
	log_must snapshot_list_test stale-snapshot-metadata \
	    "$STALE_TYPE_DATASET" volume
	destroy_dataset "$STALE_TYPE_DATASET" -r

	print 'projected-listing-passphrase' > "$KEY_FILE"
	log_must chmod 600 "$KEY_FILE"
	log_must zfs create -o mountpoint=none -o encryption=on \
	    -o keyformat=passphrase -o keylocation="file://$KEY_FILE" \
	    "$STALE_ENCRYPTED_DATASET"
	log_must snapshot_list_test stale-snapshot-metadata \
	    "$STALE_ENCRYPTED_DATASET" filesystem
	destroy_dataset "$STALE_ENCRYPTED_DATASET" -r
	log_must rm -f "$KEY_FILE"
}

function verify_filtered_fallback
{
	typeset target="$DATASET@a_newest"
	typeset txg preload="$SHIM"

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	txg=$(zfs get -H -p -o value createtxg "$target")
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='cmd_unavail' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test filter '$DATASET' '$txg' '$txg' " \
	    "> '$INJECTED_OUTPUT'"
	log_must grep -Fx cmd_unavail "$MARKER"
	[[ "$(<"$INJECTED_OUTPUT")" == "$target" ]] ||
	    log_fail "legacy fallback did not preserve exact creation-TXG filter"
	log_must rm -f "$MARKER"
}

function verify_projected_sort
{
	typeset sort_options="$1"

	log_must eval "zfs list -H -p -t snapshot -o name,available " \
	    "$sort_options '$DATASET' | cut -f1 > '$EXPECTED_OUTPUT'"
	run_injected_list count "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
	    "$sort_options"
}

function verify_projected_userrefs
{
	log_must eval "zfs list -H -p -t snapshot -o name,userrefs,available " \
	    "'$DATASET' | cut -f1-2 > '$EXPECTED_OUTPUT'"
	run_injected_list count "$INJECTED_OUTPUT" name,userrefs \
	    "$EXPECTED_OUTPUT"
}

function verify_filtered_batch_fill
{
	typeset target="$DATASET@a_newest"
	typeset txg preload="$SHIM"
	typeset -i calls

	[[ -n "$LD_PRELOAD" ]] && preload="$SHIM:$LD_PRELOAD"
	txg=$(zfs get -H -p -o value createtxg "$target")
	log_must eval "LD_PRELOAD='$preload' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MODE='count' " \
	    "ZFS_SNAPSHOT_LIST_TEST_MARKER='$MARKER' " \
	    "snapshot_list_test filter '$DATASET' '$txg' '$txg' " \
	    "> '$INJECTED_OUTPUT'"
	[[ "$(<"$INJECTED_OUTPUT")" == "$target" ]] ||
	    log_fail "filtered batch did not return only $target"
	calls=$(wc -l < "$MARKER")
	(( calls == 1 )) ||
	    log_fail "filtered result required $calls batches; expected 1"
	log_must rm -f "$MARKER"
}

function verify_mixed_type_fallback
{
	typeset columns="name,createtxg,guid"

	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 " \
	    "-o '$columns,available' '$DATASET' | cut -f1-3 " \
	    "> '$EXPECTED_OUTPUT'"
	run_injected_list cmd_unavail "$INJECTED_OUTPUT" "$columns" \
	    "$EXPECTED_OUTPUT" "-d 1" snapshot,bookmark

	printf "%s\n" "$DATASET@m_oldest" "$DATASET@z_middle" \
	    "$DATASET@a_newest" "$DATASET#normal" \
	    "$DATASET#source_destroyed" | sort > "$EXPECTED_OUTPUT"
	log_must eval "cut -f1 '$INJECTED_OUTPUT' | sort > '$BATCH_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$BATCH_OUTPUT"
	log_must rm -f "$EXPECTED_OUTPUT"
}

SHIM=$(find_shim) || log_unsupported "snapshot-list test shim not found"
log_onexit cleanup
log_assert "Projected listing preserves ordering through error paths."

log_must save_tunable SNAPSHOT_LIST_BATCH_SIZE
log_must save_tunable SNAPSHOT_LIST_BATCH_TIME_US
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1024
log_must set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 100000

log_must zfs create "$DATASET"
log_must zfs snapshot "$DATASET@m_oldest"
log_must zfs snapshot "$DATASET@z_middle"
log_must zfs snapshot "$DATASET@a_newest"
log_must zfs hold "$HOLD_TAG_ONE" "$DATASET@z_middle"
log_must zfs hold "$HOLD_TAG_TWO" "$DATASET@z_middle"
log_must zfs bookmark "$DATASET@m_oldest" "$DATASET#normal"
log_must zfs snapshot "$DATASET@bookmark_source"
log_must zfs bookmark "$DATASET@bookmark_source" \
    "$DATASET#source_destroyed"
log_must zfs destroy "$DATASET@bookmark_source"
snapexists "$DATASET@bookmark_source" && \
    log_fail "bookmark source snapshot still exists"
verify_stale_bookmark_handles
verify_stale_snapshot_metadata
verify_written_uses_legacy
verify_all_uses_legacy
run_injected_bookmark_errors
verify_projected_userrefs
verify_projected_sort "-s guid"
verify_projected_sort "-S guid"
verify_projected_sort "-s userrefs"
verify_projected_sort "-S userrefs"
verify_projected_sort "-s userrefs -S guid"
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 2
verify_filtered_batch_fill
verify_filtered_fallback
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1
run_injected_empty_batch
run_injected_eof_after_first
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1024
log_must rm -f "$EXPECTED_OUTPUT"
for error in cmd_unavail arg_unavail enotty enotsup enoent esrch; do
	log_must snapshot_list_test callback-error "$DATASET" "$error"
done
for error in enoent esrch; do
	log_must snapshot_list_test bookmark-callback-error "$DATASET" "$error"
done
run_injected_handle_enomem
verify_direct_properties
run_injected_metadata_errors
log_must eval "zfs list -H -p -t snapshot -o '$COLUMNS' '$DATASET' " \
    "> '$BATCH_OUTPUT'"
run_injected_list enomem "$INJECTED_OUTPUT"
run_injected_list real_enomem "$INJECTED_OUTPUT"
run_injected_list cmd_unavail "$INJECTED_OUTPUT"
run_injected_list enotty "$INJECTED_OUTPUT"
run_injected_list enotsup "$INJECTED_OUTPUT"
run_injected_list arg_unavail "$INJECTED_OUTPUT"
verify_mixed_type_fallback

log_must touch "$EXPECTED_OUTPUT"
run_injected_list enoent "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT"
run_injected_list esrch "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT"

printf "%s\n" "$DATASET@m_oldest" "$DATASET@z_middle" \
    "$DATASET@a_newest" > "$EXPECTED_OUTPUT"
run_injected_list enomem "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
    "-s type"
run_injected_list cmd_unavail "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
    "-s type"
run_injected_list enotty "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
    "-s type"
run_injected_list enotsup "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
    "-s type"
run_injected_list arg_unavail "$INJECTED_OUTPUT" name "$EXPECTED_OUTPUT" \
    "-s type"

run_injected_interrupt
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1
run_injected_late_failure
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1024

log_pass "Projected listing preserves ordering through error paths."
