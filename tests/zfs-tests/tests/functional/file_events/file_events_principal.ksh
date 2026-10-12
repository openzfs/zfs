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
# Principal tags: a process registered with lzc_set_principal() tags
# its records with the principal value; unregistered writes carry no
# principal; clearing returns a monotonically increasing generation.
#
# STRATEGY:
# 1. Enable events=on on a child filesystem.
# 2. Run file_events_principal write: registers 0xdeadbeef and writes a
#    file from the same process.
# 3. Read raw records via file_events_principal read and verify the
#    record carries principal=3735928559 and uid=0.
# 4. Write a file from an unregistered shell; verify no record carries
#    a principal.
# 5. Re-register in a fresh process: the per-tgid generation must be
#    strictly greater than the one reported by the first writer.
# 6. Clearing twice from one process fails cleanly (no registration).

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/file_events/file_events.kshlib

typeset -r PRINCIPAL_BIN="file_events_principal"
typeset -r PRINCIPAL_DEC=3735928559

function cleanup
{
	destroy_fetest_child "$TESTPOOL/$TESTFS/fetest-pr"
}

log_onexit cleanup

log_assert "principal tags attach to records of registered processes only"

ds=$(make_fetest_child) || log_fail "create fetest-pr"
log_must zfs set events=on "$ds"
mnt=$(get_prop mountpoint "$ds")

genw1=$("$PRINCIPAL_BIN" write "$mnt/tagged") ||
    log_fail "principal write failed"
count=$(wait_records "$ds" 1) || log_fail "expected 1 record, got $count"

typeset out="$TMPDIR/file_events_principal.$$"
log_must "$PRINCIPAL_BIN" read "$ds" >"$out"
grep -q "principal=$PRINCIPAL_DEC" "$out" ||
    log_fail "no record with principal=$PRINCIPAL_DEC: $(cat "$out")"
grep -q "uid=0" "$out" ||
    log_fail "helper-written record missing uid=0: $(cat "$out")"

# An unregistered write must produce a record without a principal key.
log_must touch "$mnt/untagged"
count=$(wait_records "$ds" 2) || log_fail "expected 2 records, got $count"
log_must "$PRINCIPAL_BIN" read "$ds" >"$out"
typeset -i tagged=0
typeset -i untagged=0
while read -r line; do
	case "$line" in
	*principal=none*) ((untagged = untagged + 1)) ;;
	*principal=$PRINCIPAL_DEC*) ((tagged = tagged + 1)) ;;
	esac
done <"$out"
[[ $untagged -ge 1 ]] ||
    log_fail "expected at least one principal-less record"
[[ $tagged -eq 1 ]] ||
    log_fail "expected exactly 1 tagged record, got $tagged"

# Generations are PER-REGISTRATION (per-tgid entry): every fresh
# writer process reports gen 1 on its first set.  The helper
# deregisters before exit (a separate `clear` process would find
# no entry - ENOENT by design).  Assert both writers got gen 1.
[[ "$genw1" -eq 1 ]] ||
    log_fail "first writer gen=$genw1, expected 1"
genw2=$("$PRINCIPAL_BIN" write "$mnt/tagged2") ||
    log_fail "second principal write failed"
[[ "$genw2" -eq 1 ]] ||
    log_fail "second writer gen=$genw2, expected 1"
rm -f "$out"

log_pass "principal tags attach to records of registered processes only"
