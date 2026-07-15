#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions and
# limitations under the License.
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
# Copyright (c) 2026, MISAPOR LAB
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

#
# DESCRIPTION:
# Regression test for issue #18426: ARC shrinker deadlock with large
# anonymous (dirty) data.
#
# When arc_c is pushed down while a large amount of dirty data is queued
# in arc_anon, sync-context writes must still be able to make progress.
# The fix adds anon_size to the overflow tolerance used by
# ARC_HDR_USE_RESERVE allocations.
#
# STRATEGY:
# 1. Limit ARC size to a small value.
# 2. Start a large asynchronous write so dirty data accumulates in
#    arc_anon.
# 3. While anon_size is large, lower ARC_MAX even further to simulate
#    the shrinker pushing arc_c below anon_size.
# 4. Verify the write completes without hanging.
# 5. Verify the file was fully written and txg_sync can drain anon data.
#

verify_runnable "both"

function cleanup
{
	[[ -n "$write_pid" ]] && kill -9 "$write_pid" 2>/dev/null
	wait "$write_pid" 2>/dev/null
	log_must rm -f "$TESTDIR/arc_anon_overflow_file"
	log_must set_tunable64 TXG_TIMEOUT "$ORIG_TXG_TIMEOUT"
	log_must set_tunable64 ARC_MAX "$ORIG_ARC_MAX"
	log_must set_tunable64 ARC_MIN "$ORIG_ARC_MIN"
}

function get_arcstat
{
	typeset field="$1"

	case "$UNAME" in
	Linux)
		awk -v f="$field" '$1 == f { print $3 }' \
		    /proc/spl/kstat/zfs/arcstats
		;;
	FreeBSD)
		sysctl -n "kstat.zfs.misc.arcstats.$field"
		;;
	*)
		log_unsupported "Unsupported platform for kstat lookup"
		;;
	esac || log_fail "get_arcstat $field failed"
}

log_onexit cleanup

ORIG_ARC_MAX="$(get_tunable ARC_MAX)"
ORIG_ARC_MIN="$(get_tunable ARC_MIN)"
ORIG_TXG_TIMEOUT="$(get_tunable TXG_TIMEOUT)"

log_assert "writes progress when arc_c is forced below anon_size"

# Use a small ARC so we can quickly push it below a modest anon_size.
TEST_ARC_MAX=$((256 * 1024 * 1024))
TEST_ARC_MIN=$((64 * 1024 * 1024))
SMALL_ARC_MAX=$((128 * 1024 * 1024))

log_must set_tunable64 ARC_MAX "$TEST_ARC_MAX"
log_must set_tunable64 ARC_MIN "$TEST_ARC_MIN"
# Keep dirty data in arc_anon longer by delaying txg_sync.
log_must set_tunable64 TXG_TIMEOUT "30000"

# Flush and drop caches to start from a predictable ARC state.
log_must sync_all_pools
case "$UNAME" in
Linux)
	log_must eval "echo 3 > /proc/sys/vm/drop_caches"
	;;
esac

log_must zfs set compression=off "$TESTPOOL/$TESTFS"

# Start a large asynchronous write (twice the test ARC size).
FILE_SIZE=$((512 * 1024 * 1024))
BLOCK_SIZE=$((1 * 1024 * 1024))
NUM_BLOCKS=$((FILE_SIZE / BLOCK_SIZE))

write_pid=""
log_must eval "file_write -o create -f $TESTDIR/arc_anon_overflow_file \
    -b $BLOCK_SIZE -c $NUM_BLOCKS -d R &"
write_pid=$!

# Wait until we have accumulated enough anonymous dirty data, then push
# ARC_MAX down below anon_size to trigger the formerly-deadly path.
lowered=0
for i in {0..29}; do
	typeset anon_size="$(get_arcstat anon_size)"
	typeset arc_c="$(get_arc_target)"
	if ((anon_size > SMALL_ARC_MAX && lowered == 0)); then
		log_must set_tunable64 ARC_MAX "$SMALL_ARC_MAX"
		log_must set_tunable64 ARC_MIN "$((SMALL_ARC_MAX / 4))"
		lowered=1
		log_note "lowered ARC_MAX to $SMALL_ARC_MAX with anon_size=$anon_size"
	fi
	if ((lowered == 1)); then
		break
	fi
	sleep 1
done

if ((lowered == 0)); then
	log_note "anon_size never exceeded threshold; checking write still succeeds"
fi

# The write must complete in a reasonable time; before the fix it could
# hang indefinitely when arc_c was forced below anon_size.
log_must timeout 60s wait "$write_pid"
write_pid=""

log_must sync_all_pools

# Verify the file is the expected size.
log_must test -s "$TESTDIR/arc_anon_overflow_file"
actual_size="$(stat -c %s "$TESTDIR/arc_anon_overflow_file" 2>/dev/null || \
    stat -f %z "$TESTDIR/arc_anon_overflow_file" 2>/dev/null)"
log_must test "$actual_size" -eq "$FILE_SIZE"

# Verify txg_sync drained the anonymous dirty data after sync.
final_anon="$(get_arcstat anon_size)"
log_must test "$final_anon" -lt "$SMALL_ARC_MAX"

log_pass "writes progress when arc_c is forced below anon_size"
