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
# IO event recording: WRITE records with io_offset/io_bytes, window
# coalescing at the default events_io_window, and window=0 which
# disables coalescing entirely.
#
# STRATEGY:
# 1. Enable events=on and events_io=on on a child filesystem.
# 2. A single 5000-byte dd write at offset 0 produces one WRITE record
#    with io_offset=0 and io_bytes=5000.
# 3. Two rapid writes within a 2s window (events_io_window=2000 ms)
#    coalesce into a single WRITE record (the fence is time-based).
# 4. Setting events_io_window=0 disables coalescing: three 100-byte
#    writes produce three WRITE records.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-io"
}

log_onexit cleanup

log_assert "IO events: WRITE records, default-window coalescing, window=0 fencing off"

ds=$(make_fetest_child) || log_fail "create fetest-io"
log_must zfs set events=on "$ds"
log_must zfs set events_io=on "$ds"
mnt=$(get_prop mountpoint "$ds")

# single write, exact fields
log_must dd if=/dev/zero of="$mnt/io1" bs=1000 count=5 conv=notrunc
count=$(wait_records "$ds" 1) || log_fail "expected 1 record, got $count"
typeset json="$TMPDIR/file_events_io.$$"
log_must zfs events -j "$ds" >"$json"
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
writes = [e for e in page["events"] if e["op"] == "WRITE"]
assert len(writes) == 1, "expected 1 WRITE, got %d" % len(writes)
w = writes[0]
assert w["io_offset"] == 0, "io_offset=%s" % w["io_offset"]
assert w["io_bytes"] == 5000, "io_bytes=%s" % w["io_bytes"]
print("write-ok")
EOF
[[ $? -eq 0 ]] || log_fail "WRITE record fields wrong"
rm -f "$json"
log_must zfs events -c "$ds" >/dev/null 2>&1 || true

# events_io_window=2000 ms: two rapid writes coalesce to one record
log_must zfs set events_io_window=2000 "$ds"
log_must dd if=/dev/zero of="$mnt/io2" bs=1500 count=1 conv=notrunc
log_must dd if=/dev/zero of="$mnt/io2" bs=1500 count=1 seek=1 conv=notrunc
zpool sync "$TESTPOOL"
count=$(wait_records "$ds" 1) || log_fail "expected 1 coalesced record, got $count"
json="$TMPDIR/file_events_io2.$$"
log_must zfs events -j "$ds" >"$json"
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
writes = [e for e in page["events"] if e["op"] == "WRITE"]
assert len(writes) == 1, "expected 1 coalesced WRITE, got %d" % len(writes)
print("coalesce-ok")
EOF
[[ $? -eq 0 ]] || log_fail "expected the two writes to coalesce"
rm -f "$json"
log_must zfs events -c "$ds" >/dev/null 2>&1 || true

# window=0: fencing disabled, no coalescing - 3 writes, 3 records
log_must zfs set events_io_window=0 "$ds"
for i in 1 2 3; do
	log_must dd if=/dev/zero of="$mnt/io3" bs=100 count=1 \
	    seek=$((i - 1)) conv=notrunc
done
count=$(wait_records "$ds" 3) || log_fail "expected 3 records, got $count"
json="$TMPDIR/file_events_io3.$$"
log_must zfs events -j "$ds" >"$json"
python3 - "$json" <<'EOF'
import json, sys
with open(sys.argv[1]) as f:
    page = json.load(f)
writes = [e for e in page["events"] if e["op"] == "WRITE"]
assert len(writes) == 3, "expected 3 WRITE, got %d" % len(writes)
print("window0-ok")
EOF
[[ $? -eq 0 ]] || log_fail "window=0 should produce 3 WRITE records"
rm -f "$json"

log_pass "IO events: WRITE records, default-window coalescing, window=0 fencing off"
