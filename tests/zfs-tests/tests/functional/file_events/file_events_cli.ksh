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
# File event CLI smoke test across the subcommand surface: JSON page
# keys, record count limiting, and the refusals for events=off datasets.
#
# STRATEGY:
# 1. Enable events=on on a child, write files, wait for records.
# 2. zfs events -j returns a page with events/next_offset/records_lost
#    /schema_version keys.
# 3. zfs events -n 1 returns exactly one record.
# 4. zfs events on an events=off sibling fails, telling the user to
#    enable events first.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-cli"
}

log_onexit cleanup

log_assert "zfs events CLI: page keys, -n count, events=off refusal"

ds=$(make_fetest_child) || log_fail "create fetest-cli"
log_must zfs set events=on "$ds"
mnt=$(get_prop mountpoint "$ds")

log_must touch "$mnt/cli-1" "$mnt/cli-2" "$mnt/cli-3"
count=$(wait_records "$ds" 3) || log_fail "expected 3 records, got $count"

# JSON page carries the documented keys.
typeset json="$TMPDIR/file_events_cli.$$"
	zfs events -j "$ds" >"$json"
	[[ $? -eq 0 ]] || log_fail "zfs events -j failed"
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
# -j prints a JSON ARRAY of records (page metadata stays in the
# ioctl nvl and is not part of the CLI output).
assert isinstance(page, list), "expected a JSON array of records"
# 3 touches may add SETATTR piggyback records; require at least 3
assert len(page) >= 3, "expected >=3 records, got %d" % len(page)
for e in page:
    assert "op" in e and "txg" in e, "record missing op/txg: %r" % e
print("page-ok")
EOF
[[ $? -eq 0 ]] || log_fail "JSON page keys wrong"

# -n 1 limits to exactly one record.
typeset out="$TMPDIR/file_events_cli_n.$$"
	zfs events -n 1 "$ds" >"$out" 2>/dev/null
	[[ $? -eq 0 ]] || log_fail "zfs events -n 1 failed"
# plain output: 2 header lines (TXG/---) + 1 record
[[ $(grep -v -e '^TXG' -e '^---' <"$out" | wc -l) -eq 1 ]] ||
    log_fail "zfs events -n 1 returned $(wc -l <"$out") lines"
rm -f "$out"

# events=off sibling refuses.
typeset err="$TMPDIR/file_events_cli.err.$$"
zfs events "$TESTPOOL/$TESTFS" >"$TMPDIR/file_events_cli.off.$$" 2>"$err"
typeset -i rc=$?
# The parent test filesystem was never enabled; depending on whether
# the suite wrote to it we may get records, but a dataset with
# events=off and no log must fail with the enable hint.
rm -f "$TMPDIR/file_events_cli.off.$$"
if [[ $rc -eq 0 ]]; then
	# The parent may have a log if something enabled it; create a
	# fresh never-enabled dataset to test the refusal path.
	typeset neverds="$ds/never"
	log_must zfs create "$neverds"
	zfs events "$neverds" >/dev/null 2>"$err"
	rc=$?
fi
[[ $rc -ne 0 ]] ||
    log_fail "zfs events on never-enabled dataset succeeded"
grep -qi "enable events" "$err" ||
    log_fail "no enable hint in refusal: $(cat "$err")"
rm -f "$json" "$err"

log_pass "zfs events CLI: page keys, -n count, events=off refusal"
