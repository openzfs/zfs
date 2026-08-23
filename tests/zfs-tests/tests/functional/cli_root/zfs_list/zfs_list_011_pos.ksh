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
# Batched snapshot listing handles iteration boundaries and ordering across
# multiple result batches.
#
# STRATEGY:
# 1. Create 1025 snapshots and verify direct iteration returns each exactly
#    once.
# 2. Verify direct-libzfs ordering across multiple small batches.
#

verify_runnable "global"
set -o pipefail

BOUNDARY_DATASET="$TESTPOOL/$TESTFS/projected_boundary"
SUBSET_DATASET="$TESTPOOL/$TESTFS/projected_subsets"
BATCH_OUTPUT="$TEST_BASE_DIR/projected_boundary_batch.$$"
FILTER_OUTPUT="$TEST_BASE_DIR/projected_boundary_filter.$$"
EXPECTED_OUTPUT="$TEST_BASE_DIR/projected_boundary_expected.$$"

function cleanup
{
	rm -f "$BATCH_OUTPUT" "$FILTER_OUTPUT" "$EXPECTED_OUTPUT"
	datasetexists "$SUBSET_DATASET" && zfs destroy -r "$SUBSET_DATASET"
	datasetexists "$BOUNDARY_DATASET" && zfs destroy -r "$BOUNDARY_DATASET"
	log_must restore_tunable SNAPSHOT_LIST_BATCH_TIME_US
	log_must restore_tunable SNAPSHOT_LIST_BATCH_SIZE
}

log_onexit cleanup
log_assert "Batched snapshot listing handles boundaries and ordering."

log_must save_tunable SNAPSHOT_LIST_BATCH_SIZE
log_must save_tunable SNAPSHOT_LIST_BATCH_TIME_US
log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 1024
log_must set_tunable32 SNAPSHOT_LIST_BATCH_TIME_US 100000

log_must zfs create "$BOUNDARY_DATASET"

typeset -i index=0
while (( index < 1025 )); do
	suffix=$(printf "%04d" "$index")
	log_must zfs snapshot "$BOUNDARY_DATASET@suffix_$suffix"
	(( index += 1 ))
done

log_must eval "snapshot_list_test filter '$BOUNDARY_DATASET' 0 0 " \
    "> '$FILTER_OUTPUT'"
typeset -i callbacks
callbacks=$(wc -l < "$FILTER_OUTPUT")
(( callbacks == 1025 )) ||
    log_fail "direct iteration delivered $callbacks callbacks; expected 1025"
# Sorting preserves duplicate names, so this compares the callback multisets.
log_must eval "sort '$FILTER_OUTPUT' > '$BATCH_OUTPUT'"
log_must eval "zfs list -H -p -t snapshot -o name,available " \
    "'$BOUNDARY_DATASET' | cut -f1 | sort > '$EXPECTED_OUTPUT'"
log_must diff "$EXPECTED_OUTPUT" "$BATCH_OUTPUT"

log_must set_tunable32 SNAPSHOT_LIST_BATCH_SIZE 2
log_must zfs create "$SUBSET_DATASET"
log_must zfs snapshot "$SUBSET_DATASET@m_oldest"
log_must zfs snapshot "$SUBSET_DATASET@z_middle"
log_must zfs snapshot "$SUBSET_DATASET@a_newest"
oldest_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@m_oldest")
middle_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@z_middle")
newest_txg=$(zfs get -H -p -o value createtxg \
    "$SUBSET_DATASET@a_newest")
(( oldest_txg < middle_txg && middle_txg < newest_txg )) ||
    log_fail "ordering snapshots do not have increasing creation TXGs"

printf "%s\n" "$SUBSET_DATASET@m_oldest" "$SUBSET_DATASET@z_middle" \
    "$SUBSET_DATASET@a_newest" > "$EXPECTED_OUTPUT"

log_must eval "snapshot_list_test sorted '$SUBSET_DATASET' " \
    "> '$BATCH_OUTPUT'"
log_must diff "$EXPECTED_OUTPUT" "$BATCH_OUTPUT"

log_pass "Batched snapshot listing handles boundaries and ordering."
