#!/bin/ksh
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
# Description:
#
# Verify zhack mosleak inject and leak reclaim behavior.
#
# Strategy:
#
# 1. Create a test pool and confirm leak scan baseline is empty.
# 2. Verify leak inject dry-run does not modify leak scan counts.
# 3. Inject a known number of leaked clone maps and space maps.
# 4. Verify scan reports exactly the injected reclaimable objects.
# 5. Verify leak reclaim dry-run does not modify scan counts.
# 6. Reclaim leaked objects and verify scan baseline is restored.
# 7. Verify reclaim is idempotent by running it again.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	poolexists "$TESTPOOL" && destroy_pool "$TESTPOOL"
	[[ -n "$SCAN_BEFORE" ]] && rm -f "$SCAN_BEFORE"
	[[ -n "$SCAN_DRY_INJECT" ]] && rm -f "$SCAN_DRY_INJECT"
	[[ -n "$SCAN_AFTER_INJECT" ]] && rm -f "$SCAN_AFTER_INJECT"
	[[ -n "$SCAN_DRY_RECLAIM" ]] && rm -f "$SCAN_DRY_RECLAIM"
	[[ -n "$SCAN_FINAL" ]] && rm -f "$SCAN_FINAL"
	[[ -n "$INJECT_DRY" ]] && rm -f "$INJECT_DRY"
	[[ -n "$INJECT_WRITE" ]] && rm -f "$INJECT_WRITE"
	[[ -n "$RECLAIM_DRY" ]] && rm -f "$RECLAIM_DRY"
	[[ -n "$RECLAIM_WRITE" ]] && rm -f "$RECLAIM_WRITE"
	[[ -n "$RECLAIM_WRITE_SECOND" ]] && rm -f "$RECLAIM_WRITE_SECOND"
}

function get_reclaimable_count
{
	typeset output_file="$1"
	typeset category="$2"
	typeset line=
	typeset count=

	line="$(grep -F "$category" "$output_file")"
	[[ -n "$line" ]] || log_fail "missing summary line: $category"

	count="${line#*reclaimable=}"
	count="${count%% *}"
	echo "$count"
}

function get_total_count
{
	typeset output_file="$1"
	typeset line=

	line="$(grep -F "total reclaimable objects:" "$output_file")"
	[[ -n "$line" ]] || log_fail "missing total reclaimable summary"
	echo "${line##*: }"
}

function assert_count_eq
{
	typeset expected="$1"
	typeset actual="$2"
	typeset message="$3"

	[[ "$expected" == "$actual" ]] || log_fail \
	    "$message: expected $expected, got $actual"
}

log_assert "zhack mosleak inject/reclaim should be observable and reversible"
log_onexit cleanup

typeset -r CLONE_INJECT=3
typeset -r SPACEMAP_INJECT=2
typeset -r TOTAL_INJECT=$((CLONE_INJECT + SPACEMAP_INJECT))

typeset SCAN_BEFORE="$(mktemp)"
typeset SCAN_DRY_INJECT="$(mktemp)"
typeset SCAN_AFTER_INJECT="$(mktemp)"
typeset SCAN_DRY_RECLAIM="$(mktemp)"
typeset SCAN_FINAL="$(mktemp)"
typeset INJECT_DRY="$(mktemp)"
typeset INJECT_WRITE="$(mktemp)"
typeset RECLAIM_DRY="$(mktemp)"
typeset RECLAIM_WRITE="$(mktemp)"
typeset RECLAIM_WRITE_SECOND="$(mktemp)"

log_must zpool create "$TESTPOOL" $DISKS

log_must eval "zhack mosleak scan $TESTPOOL > $SCAN_BEFORE"
before_clones="$(get_reclaimable_count "$SCAN_BEFORE" \
    "unreferenced DSL dir clones:")"
before_spacemaps="$(get_reclaimable_count "$SCAN_BEFORE" \
    "unreferenced SPA space maps:")"
before_total="$(get_total_count "$SCAN_BEFORE")"

assert_count_eq 0 "$before_clones" "baseline leaked clone count"
assert_count_eq 0 "$before_spacemaps" "baseline leaked space map count"
assert_count_eq 0 "$before_total" "baseline total leaked object count"

log_must eval "zhack mosleak inject -c $CLONE_INJECT -s $SPACEMAP_INJECT \
    $TESTPOOL > $INJECT_DRY"
log_must grep -F \
    "dry run: would inject $CLONE_INJECT leaked DSL clone map(s) and $SPACEMAP_INJECT leaked SPA space map(s)" \
    "$INJECT_DRY"

log_must eval "zhack mosleak scan $TESTPOOL > $SCAN_DRY_INJECT"
dry_clones="$(get_reclaimable_count "$SCAN_DRY_INJECT" \
    "unreferenced DSL dir clones:")"
dry_spacemaps="$(get_reclaimable_count "$SCAN_DRY_INJECT" \
    "unreferenced SPA space maps:")"
dry_total="$(get_total_count "$SCAN_DRY_INJECT")"

assert_count_eq "$before_clones" "$dry_clones" \
    "post-dry-run leaked clone count"
assert_count_eq "$before_spacemaps" "$dry_spacemaps" \
    "post-dry-run leaked space map count"
assert_count_eq "$before_total" "$dry_total" \
    "post-dry-run total leaked object count"

log_must zpool export "$TESTPOOL"
log_must eval "zhack mosleak inject -w -c $CLONE_INJECT -s $SPACEMAP_INJECT \
    $TESTPOOL > $INJECT_WRITE"
log_must zpool import "$TESTPOOL"
log_must grep -F \
    "injected summary: clones=$CLONE_INJECT spacemaps=$SPACEMAP_INJECT total=$TOTAL_INJECT" \
    "$INJECT_WRITE"

log_must eval "zhack mosleak scan $TESTPOOL > $SCAN_AFTER_INJECT"
after_clones="$(get_reclaimable_count "$SCAN_AFTER_INJECT" \
    "unreferenced DSL dir clones:")"
after_spacemaps="$(get_reclaimable_count "$SCAN_AFTER_INJECT" \
    "unreferenced SPA space maps:")"
after_total="$(get_total_count "$SCAN_AFTER_INJECT")"

assert_count_eq "$CLONE_INJECT" "$after_clones" \
    "post-inject leaked clone count"
assert_count_eq "$SPACEMAP_INJECT" "$after_spacemaps" \
    "post-inject leaked space map count"
assert_count_eq "$TOTAL_INJECT" "$after_total" \
    "post-inject total leaked object count"

log_must eval "zhack mosleak reclaim $TESTPOOL > $RECLAIM_DRY"
log_must grep -F "dry run: no changes made (re-run with -w to reclaim)" \
    "$RECLAIM_DRY"

log_must eval "zhack mosleak scan $TESTPOOL > $SCAN_DRY_RECLAIM"
dry_reclaim_clones="$(get_reclaimable_count "$SCAN_DRY_RECLAIM" \
    "unreferenced DSL dir clones:")"
dry_reclaim_spacemaps="$(get_reclaimable_count "$SCAN_DRY_RECLAIM" \
    "unreferenced SPA space maps:")"
dry_reclaim_total="$(get_total_count "$SCAN_DRY_RECLAIM")"

assert_count_eq "$after_clones" "$dry_reclaim_clones" \
    "post-reclaim-dry-run leaked clone count"
assert_count_eq "$after_spacemaps" "$dry_reclaim_spacemaps" \
    "post-reclaim-dry-run leaked space map count"
assert_count_eq "$after_total" "$dry_reclaim_total" \
    "post-reclaim-dry-run total leaked object count"

log_must zpool export "$TESTPOOL"
log_must eval "zhack mosleak reclaim -w $TESTPOOL > $RECLAIM_WRITE"
log_must zpool import "$TESTPOOL"
log_must grep -F "reclaimed $TOTAL_INJECT leaked MOS objects" \
    "$RECLAIM_WRITE"

log_must zpool export "$TESTPOOL"
log_must eval "zhack mosleak reclaim -w $TESTPOOL > $RECLAIM_WRITE_SECOND"
log_must zpool import "$TESTPOOL"
log_must grep -F "nothing to reclaim" "$RECLAIM_WRITE_SECOND"

log_must eval "zhack mosleak scan $TESTPOOL > $SCAN_FINAL"
final_clones="$(get_reclaimable_count "$SCAN_FINAL" \
    "unreferenced DSL dir clones:")"
final_spacemaps="$(get_reclaimable_count "$SCAN_FINAL" \
    "unreferenced SPA space maps:")"
final_total="$(get_total_count "$SCAN_FINAL")"

assert_count_eq 0 "$final_clones" "post-reclaim leaked clone count"
assert_count_eq 0 "$final_spacemaps" "post-reclaim leaked space map count"
assert_count_eq 0 "$final_total" "post-reclaim total leaked object count"

log_pass "zhack mosleak inject/reclaim behaved correctly"
