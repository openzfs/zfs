#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
# shellcheck disable=SC2154
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# DESCRIPTION:
#	Fast-dedup (FDT) extension must not append a DVA allocated at a
#	post-raidz-expansion width onto a DDT entry whose single physical
#	birth predates the expansion. A BP cannot represent per-DVA geometry
#	epochs, permitting wrong-layout reads. Verify the extension is refused
#	(the write falls back to a coherent non-dedup write) while same-epoch
#	extension still works.
#
# STRATEGY:
#	1. Create a fast-dedup RAIDZ1 pool (ashift=12) with dedup=on.
#	2. Control: on the un-expanded pool, copies=1 -> copies=2 of the
#	   same content must extend the DDT entry (two equal-ASIZE DVAs).
#	3. Write a copies=1 block, expand raidz1 3->4, cross the width
#	   marker, then request copies=2 of the same content.
#	4. Assert the entry is NOT extended across the width boundary: it
#	   stays one old-width DVA and the new file gets a fresh non-dedup
#	   BP; then scrub clean.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

log_assert "FDT extension does not mix RAIDZ expansion geometry epochs"

typeset -r devs=4
typeset -r dev_size_mb=256
typeset -r block_size=128k

typeset -a disks
typeset mnt="$TEST_BASE_DIR/raidz-fdt-mnt"
typeset ddt_dump="$TEST_BASE_DIR/raidz-fdt-ddt.$$"
typeset bp_dump="$TEST_BASE_DIR/raidz-fdt-bp.$$"
typeset pool_guid

typeset -r same_epoch_re='^index [[:xdigit:]]+ refcnt 2 phys 0 DVA\[0\]=<0:[[:xdigit:]]+:30000> DVA\[1\]=<0:[[:xdigit:]]+:30000> '
typeset -r mixed_epoch_re='^index [[:xdigit:]]+ refcnt 2 phys 0 DVA\[0\]=<0:[[:xdigit:]]+:30000> DVA\[1\]=<0:[[:xdigit:]]+:2c000> '
typeset -r fixed_entry_re='^index [[:xdigit:]]+ refcnt 1 phys 0 DVA\[0\]=<0:[[:xdigit:]]+:30000> '
typeset -r fresh_bp_re='L0 DVA\[0\]=<0:[[:xdigit:]]+:2c000> DVA\[1\]=<0:[[:xdigit:]]+:2c000> \[L0 .* unique double .*size=20000L/20000P birth='

log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable SCRUB_AFTER_EXPAND
log_must save_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES

function cleanup
{
	if poolexists "$TESTPOOL"; then
		destroy_pool "$TESTPOOL"
	fi

	for ((i = 0; i < devs; i++)); do
		[[ -n "${disks[$i]}" ]] &&
		    log_must rm -f "${disks[$i]}"
	done

	log_must rm -f "$ddt_dump" "$bp_dump"
	log_must rm -rf "$mnt"

	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable SCRUB_AFTER_EXPAND
	log_must restore_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES
}

function dump_ddt
{
	log_must eval "zdb -CDDDDD '$TESTPOOL' > '$ddt_dump'"
	log_must grep -Eq "^[[:space:]]*pool_guid: ${pool_guid}\$" "$ddt_dump"
	log_note "$(grep '^index ' "$ddt_dump")"
}

function ddt_logs_are_drained
{
	typeset headers

	headers=$(grep -Ec '^DDT-log-.* entries=[0-9]+$' "$ddt_dump")
	if ((headers == 0)); then
		return 0
	fi

	((headers == 2)) || return 1
	awk '
		/^DDT-log-.* entries=/ && $NF != "entries=0" { bad = 1 }
		END { exit bad }
	' "$ddt_dump"
}

function drain_ddt_logs
{
	typeset -i i

	for ((i = 0; i < 10; i++)); do
		sync_pool "$TESTPOOL" true
		dump_ddt
		if ddt_logs_are_drained; then
			return
		fi
	done

	log_fail "DDT logs did not drain after 10 forced syncs"
}

function assert_one_ddt_entry
{
	typeset count

	ddt_logs_are_drained ||
	    log_fail "cannot count persistent DDT entries before logs drain"
	count=$(awk '/^index / { n++ } END { print n + 0 }' "$ddt_dump")
	((count == 1)) ||
	    log_fail "expected one DDT entry, found $count"
}

#
# No pool_guid pinning here: zdb never prints the pool config for a
# dataset target, and every phase pins the GUID via dump_ddt anyway.
#
function dump_bp
{
	log_must eval \
	    "zdb -ddddddbbbbbb '$TESTPOOL/' '$obj' > '$bp_dump'"
}

log_onexit cleanup

log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 SCRUB_AFTER_EXPAND 0
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0

log_must mkdir -p "$mnt"

for ((i = 0; i < devs; i++)); do
	disks[i]="$TEST_BASE_DIR/raidz-fdt-dev-$i"
	log_must truncate -s "${dev_size_mb}M" "${disks[$i]}"
done

log_must zpool create -f \
    -o ashift=12 \
    -o feature@fast_dedup=enabled \
    -o feature@raidz_expansion=enabled \
    -o feature@block_cloning=disabled \
    -O mountpoint="$mnt" \
    -O dedup=sha256 \
    -O compression=off \
    -O recordsize="$block_size" \
    -O copies=1 \
    -O primarycache=metadata \
    -O xattr=sa \
    "$TESTPOOL" raidz1 \
    "${disks[0]}" "${disks[1]}" "${disks[2]}"

pool_guid=$(zpool get -H -o value guid "$TESTPOOL")
[[ -n "$pool_guid" ]] ||
    log_fail "could not get GUID for $TESTPOOL"

#
# Control: same-geometry copies=1 -> copies=2 extension must still work.
#
log_must dd if=/dev/urandom of="$mnt/control-1" \
    bs="$block_size" count="1"
sync_pool "$TESTPOOL"

log_must zfs set copies=2 "$TESTPOOL"
log_must dd if="$mnt/control-1" of="$mnt/control-2" \
    bs="$block_size" count="1"
sync_pool "$TESTPOOL"

drain_ddt_logs
assert_one_ddt_entry
log_must grep -Eq "$same_epoch_re" "$ddt_dump"

# Remove the control so the target phase has exactly one DDT entry.
log_must rm "$mnt/control-1" "$mnt/control-2"
sync_pool "$TESTPOOL"
drain_ddt_logs
log_must grep -q 'All DDTs are empty' "$ddt_dump"

#
# Establish one old-width physical copy.
#
log_must zfs set copies=1 "$TESTPOOL"
log_must dd if=/dev/urandom of="$mnt/target-1" \
    bs="$block_size" count="1"
sync_pool "$TESTPOOL"

#
# Expand 3 -> 4 and cross the installed logical-width marker.
#
log_must zpool attach "$TESTPOOL" raidz1-0 "${disks[3]}"
log_must zpool wait -t raidz_expand "$TESTPOOL"
# re_txg = C + TXG_CONCURRENT_STATES in raidz_reflow_complete_sync().
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"
sync_pool "$TESTPOOL"

#
# Request one additional physical copy of the existing FDT entry.
#
log_must zfs set copies=2 "$TESTPOOL"
log_must dd if="$mnt/target-1" of="$mnt/target-2" \
    bs="$block_size" count="1"
sync_pool "$TESTPOOL"

drain_ddt_logs
assert_one_ddt_entry

#
# Exact RED diagnostic. Stop before scrub because repair of this shape can
# exceed DVA[1]'s allocation.
#
if grep -Eq "$mixed_epoch_re" "$ddt_dump"; then
	log_fail "FDT extended across RAIDZ width epochs: " \
	    "$(grep '^index ' "$ddt_dump")"
fi

#
# GREEN: the original DDT phys remains refcnt=1 and one old-width DVA.
#
log_must grep -Eq "$fixed_entry_re" "$ddt_dump"
if grep '^index ' "$ddt_dump" | grep -q 'DVA\[1\]'; then
	log_fail "fixed DDT entry unexpectedly has DVA[1]"
fi

#
# target-2 must be a fresh ordinary two-copy BP at the new width.
#
typeset obj
obj=$(get_objnum "$mnt/target-2")
dump_bp

typeset bp_line
bp_line=$(grep -m 1 'L0 DVA' "$bp_dump")
[[ -n "$bp_line" ]] ||
	log_fail "target-2 L0 BP not found"
log_note "$bp_line"

log_must grep -Eq "$fresh_bp_re" "$bp_dump"

typeset births logical_birth physical_birth
births=$(print -r -- "$bp_line" | sed -n \
    's/.*birth=\([0-9][0-9]*\)L\/\([0-9][0-9]*\)P.*/\1 \2/p')
[[ -n "$births" ]] ||
	log_fail "target-2 BP birth not found"

logical_birth=${births%% *}
physical_birth=${births##* }
((logical_birth == physical_birth)) ||
	log_fail "fresh target-2 BP has split birth " \
	"$logical_birth/$physical_birth"

log_must cmp "$mnt/target-1" "$mnt/target-2"

log_must zpool scrub -w "$TESTPOOL"
log_must check_pool_status "$TESTPOOL" "scan" "with 0 errors"
log_must check_pool_status "$TESTPOOL" "scan" "repaired 0B"
log_must check_pool_status "$TESTPOOL" "errors" \
    "No known data errors"

log_must cmp "$mnt/target-1" "$mnt/target-2"

log_pass "FDT did not extend across RAIDZ expansion geometry epochs"
