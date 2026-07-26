#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Dependent iteration with batching includes snapshot clones.
#
# STRATEGY:
# 1. Create three snapshots and clone one into a sibling dataset.
# 2. Verify batched dependent iteration reports every snapshot and the clone.
# 3. Verify simple batched iteration reports the same dependent datasets.
#

verify_runnable "global"
set -o pipefail

DATASET="$TESTPOOL/$TESTFS/list_dependents"
CLONE="$TESTPOOL/$TESTFS/list_dependents_clone"
OUTPUT="$TEST_BASE_DIR/list_dependents_output.$$"
EXPECTED_OUTPUT="$TEST_BASE_DIR/list_dependents_expected.$$"

function cleanup
{
	rm -f "$OUTPUT" "$EXPECTED_OUTPUT"
	datasetexists "$CLONE" && zfs destroy -r "$CLONE"
	datasetexists "$DATASET" && zfs destroy -r "$DATASET"
}

log_onexit cleanup
log_assert "Dependent iteration with batching discovers snapshot clones."

log_must zfs create "$DATASET"
log_must zfs snapshot "$DATASET@m_oldest"
log_must zfs snapshot "$DATASET@z_middle"
log_must zfs snapshot "$DATASET@a_newest"
log_must zfs clone -o mountpoint=none "$DATASET@m_oldest" "$CLONE"

printf "%s\n" "$DATASET@m_oldest" "$DATASET@z_middle" \
    "$DATASET@a_newest" "$CLONE" | sort > "$EXPECTED_OUTPUT"
log_must eval "snapshot_list_test dependents '$DATASET' | sort > '$OUTPUT'"
log_must diff "$EXPECTED_OUTPUT" "$OUTPUT"
log_must eval "snapshot_list_test dependents-simple '$DATASET' | sort " \
    "> '$OUTPUT'"
log_must diff "$EXPECTED_OUTPUT" "$OUTPUT"

log_pass "Dependent iteration with batching discovers snapshot clones."
