#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Snapshot and bookmark listing remains consistent across dataset types,
# object counts, names, properties, ordering, and concurrent mutation.
#
# STRATEGY:
# 1. Compare listing output with the full-stat path for filesystems, clones,
#    volumes, snapshots, bookmarks, sort keys, and JSON output.
# 2. Verify empty output and exact counts at 1023, 1024, and 1025 snapshots.
# 3. Verify maximum-length snapshot and bookmark names.
# 4. Verify default, tied, explicit-name, and mixed-type ordering.
# 5. Compare all 127 nonempty subsets of seven common properties with full
#    stats.
# 6. List snapshots while another process creates, renames, and destroys one.
#

verify_runnable "global"
set -o pipefail

DATASET="$TESTPOOL/$TESTFS/list_generic"
CLONE="$TESTPOOL/$TESTFS/list_generic_clone"
VOLUME="$TESTPOOL/$TESTFS/list_generic_vol"
BOUNDARY_DATASET="$TESTPOOL/$TESTFS/list_generic_boundary"
SUBSET_DATASET="$TESTPOOL/$TESTFS/list_generic_subsets"
MAX_DATASET="$TESTPOOL/$TESTFS/list_generic_max_name"
DATASET_MOUNT="$TESTDIR/list_generic"
CLONE_MOUNT="$TESTDIR/list_generic_clone"
OUTPUT="$TEST_BASE_DIR/list_generic_output.$$"
LEGACY_OUTPUT="$TEST_BASE_DIR/list_generic_legacy.$$"
EXPECTED_OUTPUT="$TEST_BASE_DIR/list_generic_expected.$$"
RACE_LOG="$TEST_BASE_DIR/list_generic_race.$$"
COLUMNS="createtxg,creation,guid,name,type,userrefs,objsetid"
PROPERTIES="createtxg creation guid name type userrefs objsetid"
HOLD_TAG="list-generic"
race_pid=""

function cleanup
{
	[[ -n "$race_pid" ]] && kill "$race_pid" >/dev/null 2>&1
	[[ -n "$race_pid" ]] && wait "$race_pid" >/dev/null 2>&1
	zfs release "$HOLD_TAG" "$DATASET@after_append" >/dev/null 2>&1
	zfs release "$HOLD_TAG" "$VOLUME@second" >/dev/null 2>&1
	rm -f "$OUTPUT" "$LEGACY_OUTPUT" "$EXPECTED_OUTPUT" "$RACE_LOG"
	datasetexists "$CLONE" && zfs destroy -r "$CLONE"
	datasetexists "$VOLUME" && zfs destroy -r "$VOLUME"
	datasetexists "$DATASET" && zfs destroy -r "$DATASET"
	datasetexists "$MAX_DATASET" && zfs destroy -r "$MAX_DATASET"
	datasetexists "$SUBSET_DATASET" && zfs destroy -r "$SUBSET_DATASET"
	datasetexists "$BOUNDARY_DATASET" && zfs destroy -r "$BOUNDARY_DATASET"
}

function compare_full_stat
{
	typeset dataset="$1"
	typeset object_types="$2"
	typeset property
	typeset projected_columns="used,available,referenced,refer,mountpoint"
	projected_columns="$projected_columns,logicalreferenced,lrefer,defer_destroy"

	for property in createtxg creation guid name type userrefs objsetid; do
		log_must eval "zfs list -H -p -t '$object_types' -d 1 " \
		    "-s '$property' -o '$COLUMNS' '$dataset' > '$OUTPUT'"
		log_must eval "zfs list -H -p -t '$object_types' -d 1 " \
		    "-s '$property' -s test:force-legacy-iterator " \
		    "-o '$COLUMNS' '$dataset' > '$LEGACY_OUTPUT'"
		log_must diff "$LEGACY_OUTPUT" "$OUTPUT"
	done

	log_must eval "zfs list -j -p -t '$object_types' -d 1 " \
	    "-o '$COLUMNS' '$dataset' > '$OUTPUT'"
	log_must eval "zfs list -j -p -t '$object_types' -d 1 " \
	    "-s test:force-legacy-iterator " \
	    "-o '$COLUMNS' '$dataset' > '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"

	log_must eval "zfs list -t '$object_types' -d 1 -o '$COLUMNS' " \
	    "'$dataset' > '$OUTPUT'"
	log_must eval "zfs list -t '$object_types' -d 1 " \
	    "-s test:force-legacy-iterator " \
	    "-o '$COLUMNS' '$dataset' > '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"

	log_must eval "zfs list -H -p -t '$object_types' -d 1 " \
	    "-o '$projected_columns' '$dataset' > '$OUTPUT'"
	log_must eval "zfs list -H -p -t '$object_types' -d 1 " \
	    "-s test:force-legacy-iterator -o '$projected_columns' '$dataset' " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"
}

function compare_json_name
{
	typeset dataset="$1"
	typeset object_types="$2"

	log_must eval "zfs list -j -p -t '$object_types' -d 1 -o name " \
	    "'$dataset' > '$OUTPUT'"
	log_must eval "zfs list -j -p -t '$object_types' -d 1 " \
	    "-s test:force-legacy-iterator " \
	    "-o name '$dataset' > '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"

	log_must eval "zfs list -j --json-int -t '$object_types' -d 1 " \
	    "-o name '$dataset' > '$OUTPUT'"
	log_must eval "zfs list -j --json-int -t '$object_types' -d 1 " \
	    "-s test:force-legacy-iterator -o name '$dataset' " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"
}

function verify_count
{
	typeset expected="$1"
	typeset actual

	actual=$(zfs list -H -t snapshot -o name "$BOUNDARY_DATASET" | wc -l) ||
	    log_fail "failed to list snapshots for $BOUNDARY_DATASET"
	(( actual == expected )) ||
	    log_fail "expected $expected snapshots, found $actual"
}

function compare_order
{
	typeset types="$1"
	typeset sort_options="$2"

	log_must eval "zfs list -H -p -t '$types' -o name $sort_options " \
	    "'$SUBSET_DATASET' > '$OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$OUTPUT"
	log_must eval "zfs list -H -p -t '$types' -o name $sort_options " \
	    "-s test:force-legacy-iterator '$SUBSET_DATASET' " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$EXPECTED_OUTPUT" "$LEGACY_OUTPUT"
}

function compare_subset
{
	typeset columns="$1"

	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 " \
	    "-o '$columns' '$SUBSET_DATASET' > '$OUTPUT'"
	log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 " \
	    "-s test:force-legacy-iterator -o '$columns' '$SUBSET_DATASET' " \
	    "> '$LEGACY_OUTPUT'"
	log_must diff "$LEGACY_OUTPUT" "$OUTPUT"
}

log_onexit cleanup
log_assert "Snapshot and bookmark listing handles generic listing cases."

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
snapexists "$DATASET@bookmark_source" &&
    log_fail "bookmark source snapshot still exists"

log_must zfs clone -o mountpoint="$CLONE_MOUNT" "$DATASET@base" "$CLONE"
log_must file_write -o create -f "$CLONE_MOUNT/payload" \
    -b 131072 -c 4 -d R
log_must zfs snapshot "$CLONE@first"
log_must rm "$CLONE_MOUNT/payload"
log_must zfs snapshot "$CLONE@after_remove"
log_must zfs bookmark "$CLONE@first" "$CLONE#first"

log_must zfs create -V 64M "$VOLUME"
log_must zfs snapshot "$VOLUME@first"
log_must zfs snapshot "$VOLUME@second"
log_must zfs hold "$HOLD_TAG" "$VOLUME@second"
log_must zfs bookmark "$VOLUME@first" "$VOLUME#first"

compare_full_stat "$DATASET" snapshot
compare_full_stat "$DATASET" snapshot,bookmark
compare_full_stat "$CLONE" snapshot
compare_full_stat "$CLONE" snapshot,bookmark
compare_full_stat "$VOLUME" snapshot
compare_full_stat "$VOLUME" snapshot,bookmark
compare_full_stat "$DATASET" bookmark
compare_json_name "$DATASET" bookmark
compare_json_name "$DATASET" snapshot,bookmark

log_must zfs create "$BOUNDARY_DATASET"
log_must eval "zfs list -H -p -t snapshot,bookmark -d 1 -o name " \
    "'$BOUNDARY_DATASET' > '$OUTPUT'"
[[ ! -s "$OUTPUT" ]] || log_fail "empty dataset produced objects"

typeset -i index=0
while (( index < 1023 )); do
	suffix=$(printf "%04d" "$index")
	log_must zfs snapshot "$BOUNDARY_DATASET@suffix_$suffix"
	(( index += 1 ))
done
verify_count 1023
log_must zfs snapshot "$BOUNDARY_DATASET@suffix_1023"
verify_count 1024
log_must zfs snapshot "$BOUNDARY_DATASET@suffix_1024"
verify_count 1025

log_must eval "zfs list -H -p -t snapshot -o '$COLUMNS' " \
    "'$BOUNDARY_DATASET' > '$OUTPUT'"
log_must eval "zfs list -H -p -t snapshot " \
    "-s test:force-legacy-iterator -o '$COLUMNS' '$BOUNDARY_DATASET' " \
    "> '$LEGACY_OUTPUT'"
log_must diff "$LEGACY_OUTPUT" "$OUTPUT"

log_must zfs create "$MAX_DATASET"
typeset -i max_component_length
(( max_component_length = 255 - ${#MAX_DATASET} - 1 ))
max_name=$(printf "%${max_component_length}s" "" | tr ' ' s)
too_long_name="${max_name}s"
full_name="$MAX_DATASET@$max_name"
(( ${#full_name} == 255 )) ||
    log_fail "failed to construct a 255-byte snapshot name"
log_must zfs snapshot "$full_name"
log_must zfs bookmark "$full_name" "$MAX_DATASET#$max_name"
log_mustnot zfs snapshot "$MAX_DATASET@$too_long_name"
log_must eval "zfs list -H -t snapshot,bookmark -d 1 -o name " \
    "'$MAX_DATASET' > '$OUTPUT'"
log_must grep -Fx "$full_name" "$OUTPUT"
log_must grep -Fx "$MAX_DATASET#$max_name" "$OUTPUT"

log_must zfs create "$SUBSET_DATASET"
log_must zfs snapshot "$SUBSET_DATASET@m_oldest"
log_must sleep 1.1
log_must zfs snapshot "$SUBSET_DATASET@z_middle"
log_must sleep 1.1
log_must zfs snapshot "$SUBSET_DATASET@a_newest"
oldest_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@m_oldest")
middle_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@z_middle")
newest_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@a_newest")
(( oldest_txg < middle_txg && middle_txg < newest_txg )) ||
    log_fail "ordering snapshots do not have increasing creation TXGs"

log_must zfs bookmark "$SUBSET_DATASET@m_oldest" \
    "$SUBSET_DATASET#m_oldest"
log_must zfs bookmark "$SUBSET_DATASET@z_middle" \
    "$SUBSET_DATASET#z_middle"
log_must zfs bookmark "$SUBSET_DATASET@a_newest" \
    "$SUBSET_DATASET#a_newest"

printf "%s\n" "$SUBSET_DATASET@m_oldest" "$SUBSET_DATASET@z_middle" \
    "$SUBSET_DATASET@a_newest" > "$EXPECTED_OUTPUT"
compare_order snapshot ""
compare_order snapshot "-s creation"
compare_order snapshot "-s type"
compare_order snapshot "-S type"

printf "%s\n" "$SUBSET_DATASET@a_newest" "$SUBSET_DATASET@z_middle" \
    "$SUBSET_DATASET@m_oldest" > "$EXPECTED_OUTPUT"
compare_order snapshot "-S creation"

printf "%s\n" "$SUBSET_DATASET@a_newest" "$SUBSET_DATASET@m_oldest" \
    "$SUBSET_DATASET@z_middle" > "$EXPECTED_OUTPUT"
compare_order snapshot "-s name"

printf "%s\n" "$SUBSET_DATASET@z_middle" "$SUBSET_DATASET@m_oldest" \
    "$SUBSET_DATASET@a_newest" > "$EXPECTED_OUTPUT"
compare_order snapshot "-S name"

printf "%s\n" "$SUBSET_DATASET@m_oldest" "$SUBSET_DATASET@z_middle" \
    "$SUBSET_DATASET@a_newest" "$SUBSET_DATASET#a_newest" \
    "$SUBSET_DATASET#m_oldest" "$SUBSET_DATASET#z_middle" \
    > "$EXPECTED_OUTPUT"
compare_order snapshot,bookmark "-d 1"

set -A property_array $PROPERTIES
typeset -i mask property_index
typeset columns separator
for (( mask = 1; mask < 128; mask += 1 )); do
	columns=""
	separator=""
	for (( property_index = 0; property_index < 7; property_index += 1 )); do
		if (( mask & (1 << property_index) )); then
			columns="${columns}${separator}${property_array[property_index]}"
			separator=","
		fi
	done
	compare_subset "$columns"
done

log_must zfs destroy "$BOUNDARY_DATASET@suffix_1024"
verify_count 1024
(
	typeset -i race_index=0
	while (( race_index < 64 )); do
		zfs snapshot "$BOUNDARY_DATASET@zz_race" || exit 1
		zfs rename "$BOUNDARY_DATASET@zz_race" \
		    "$BOUNDARY_DATASET@zz_renamed" || exit 1
		zfs destroy "$BOUNDARY_DATASET@zz_renamed" || exit 1
		(( race_index += 1 ))
	done
) >"$RACE_LOG" 2>&1 &
race_pid=$!

for (( index = 0; index < 64; index += 1 )); do
	log_must eval "zfs list -H -p -t snapshot -o '$COLUMNS' " \
	    "'$BOUNDARY_DATASET' > /dev/null"
done
if ! wait "$race_pid"; then
	cat "$RACE_LOG"
	log_fail "concurrent snapshot mutation failed"
fi
race_pid=""

log_pass "Snapshot and bookmark listing handles generic listing cases."
