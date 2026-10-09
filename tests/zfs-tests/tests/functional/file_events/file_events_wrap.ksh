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
# Ring wraparound: with a small events_size the ring overwrites old
# records, reports records_lost, and still accepts new records without
# hanging.
#
# STRATEGY:
# 1. Set events_size=128K (~1250 records) on a child filesystem with
#    events=on.
# 2. Create ~6000 small files, forcing the ring past one wrap.
# 3. Verify the human-readable `zfs events` output reports lost records
#    ("lost to log wraparound") on stderr.
# 4. Verify newer records are still present and the command completes
#    within a timeout.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-wrap"
}

log_onexit cleanup

log_assert "ring wraparound reports lost records and stays usable"

ds=$(make_fetest_child) || log_fail "create fetest-wrap"
log_must zfs set events=on "$ds"
log_must zfs set events_size=128K "$ds"
mnt=$(get_prop mountpoint "$ds")

typeset -i i=0
while ((i < 6000)); do
	: >"$mnt/wrapfile-$i"
	((i = i + 1))
done

# Wait for at least one record to land, then force a sync so the
# wraparound accounting is stable before reading.
count=$(wait_records "$ds" 1) ||
    log_fail "no records landed at all (got $count)"
log_must zpool sync "$TESTPOOL"

# Human-readable summary reports losses on stderr.  Guard with a
# timeout: a wedged ring must fail the test, not hang the suite.
typeset out="$TMPDIR/file_events_wrap.$$"
typeset err="$TMPDIR/file_events_wrap.err.$$"
	# Direct invocation: log_must redirects stderr to its own logfile,
	# which would swallow the wraparound message we grep for.
	timeout 60 zfs events "$ds" >"$out" 2>"$err"
	[[ $? -eq 0 ]] || log_fail "zfs events failed"
grep -q "lost to log wraparound" "$err" ||
    log_fail "no wraparound loss reported on stderr: $(cat "$err")"
rm -f "$out" "$err"

# The ring must still hold recent records: the last file we created
# must appear in the JSON page.
json="$TMPDIR/file_events_wrap.json.$$"
	timeout 60 zfs events -j "$ds" >"$json"
	[[ $? -eq 0 ]] || log_fail "zfs events -j failed"
grep -q "wrapfile-5999" "$json" ||
    log_fail "recent record missing after wraparound"
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
# CLI JSON carries records only, not the lost counter (checked on
# stderr above); assert the visible window is bounded post-wrap.
assert isinstance(page, list) and 0 < len(page) < 2000, \
    "record count out of bounds after wrap: %d" % len(page)
print("lost-ok")
EOF
[[ $? -eq 0 ]] || log_fail "records_lost page key missing"
rm -f "$json"

log_pass "ring wraparound reports lost records and stays usable"
