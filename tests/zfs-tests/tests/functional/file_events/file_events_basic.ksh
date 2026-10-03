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

#
# Copyright (c) 2026 by Benjamin Hodgens <ben@hodgens.net>
#

# DESCRIPTION:
# Basic file event recording: CREATE, RENAME, TRUNCATE and REMOVE
# operations appear in 'zfs events -j' for a dataset with events=on,
# with the expected name fields and nondecreasing txgs.  A sibling
# dataset with events=off records nothing.
#
# STRATEGY:
# 1. Create a child filesystem, enable events=on, and a second child
#    left at events=off.
# 2. Create, rename, truncate and remove a file on the first child.
# 3. Verify each operation produced a JSON record with the expected
#    fields and that txg never decreases across the ordered ops.
# 4. Verify the events=off sibling produced no records.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-on"
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-off"
}

log_onexit cleanup

log_assert "file events record CREATE/RENAME/TRUNCATE/REMOVE with correct fields"

set -A ops

ds=$(make_fetest_child) || log_fail "create fetest-on"
log_must zfs rename "$ds" "$TESTPOOL/$TESTFS/fetest-on"
ds="$TESTPOOL/$TESTFS/fetest-on"
log_must zfs set events=on "$ds"

offds=$(make_fetest_child) || log_fail "create fetest-off"
log_must zfs rename "$offds" "$TESTPOOL/$TESTFS/fetest-off"
offds="$TESTPOOL/$TESTFS/fetest-off"

mnt=$(get_prop mountpoint "$ds")

log_must touch "$mnt/afile"
log_must mv "$mnt/afile" "$mnt/bfile"
log_must truncate -s 4096 "$mnt/bfile"
log_must rm "$mnt/bfile"

count=$(wait_records "$ds" 4) || log_fail "expected 4 records, got $count"

typeset json="$TMPDIR/file_events_basic.$$"
log_must zfs events -j "$ds" >"$json"

# op name pairs expected in txg order
typeset expected_ops="CREATE RENAME TRUNCATE REMOVE"
for op in $expected_ops; do
	# NOTE: print_event emits compact JSON ("op":"X", no space).
	if ! grep -q "\"op\":\"$op\"" "$json"; then
		log_fail "missing $op record"
	fi
done

# Record names are dataset-relative basenames, never absolute paths
# (verified on the wire; see events-schema-e2e.sh path-assertion note).
grep -q '"name":"afile"' "$json" ||
    log_fail "CREATE record missing name afile"
grep -q '"old_name":"afile"' "$json" ||
    log_fail "RENAME record missing old_name"
grep -q '"name":"bfile"' "$json" ||
    log_fail "RENAME/TRUNCATE/REMOVE record missing name bfile"
grep -q '"old_size":0' "$json" || log_fail "TRUNCATE missing old_size"
grep -q '"new_size":4096' "$json" || log_fail "TRUNCATE missing new_size"

# txg must be nondecreasing in record order.
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
txgs = [e["txg"] for e in page["events"]]
assert all(a <= b for a, b in zip(txgs, txgs[1:])), "txg decreased: %s" % txgs
print("txgs-ok")
EOF
[[ $? -eq 0 ]] || log_fail "txg ordering violated"

# events=off sibling must have no records.
offcount=$(wait_records "$offds" 0)
[[ "$offcount" -eq 0 ]] ||
    log_fail "events=off sibling recorded $offcount records"

rm -f "$json"
log_pass "file events record CREATE/RENAME/TRUNCATE/REMOVE with correct fields"
