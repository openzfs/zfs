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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that the Direct I/O / cache-disabled dbuf cache allowance
# (dbuf_cache_extra, reported as dbufstats.cache_extra_bytes) grows while a
# Direct I/O workload churns L1 indirect dbufs through the dbuf LRU cache,
# and decays again once that workload is gone.
#
# Direct I/O never fills the ARC with file data, so the normal arc_c-driven
# dbuf budget (arc_c >> dbuf_cache_shift) never grows and the L1 indirect
# dbufs such a workload walks would be evicted and rebuilt on every use.
# dbuf_cache_adjust_tick() then grows the dbuf-owned allowance to keep them
# resident (see dbuf_arc_underutilized()).
#
# STRATEGY:
# 1. Pump the ARC target (arc_c) down below arc_c_max so dbuf_cache_extra
#    has room to grow, and raise dbuf_cache_shift so the normal dbuf budget
#    is small enough that the test file's indirect blocks overflow it.
# 2. Create a file with a small recordsize on a metadata-only (or direct I/O)
#    dataset, and churn it with random Direct I/O reads (fio --direct=1).
# 3. Sample dbufstats.cache_extra_bytes: it must climb above its start value,
#    unless the allowance is already pinned at its ceiling
#    (arc_c_max >> dbuf_cache_extra_max_shift), in which case the Direct I/O
#    churn must still be driving dbuf evictions.
# 4. Delete the file (freeing its dbufs) and sample again: cache_extra_bytes
#    must fall back toward zero.
#
# NOTE: requires the zfs module to be rebuilt with the dbufstats
# "cache_extra_bytes" kstat field.  fio is required on Linux; on FreeBSD the
# test passes (with a warning) when fio is not installed.
#

verify_runnable "global"

if ! command -v fio >/dev/null 2>&1; then
	log_note "fio is not installed; Direct I/O dbuf cache churn is not run"
	if is_linux; then
		log_unsupported "fio is required"
	fi
	log_pass "fio not available"
fi

#
# Tunables/knobs.  If the host has an unusual ARC or disk geometry these are
# the values to adjust; the assertions themselves are relative (extra rises,
# then falls) so they only need the churn to actually overflow the budget.
#
typeset RECORDSIZE=8k
typeset FILE_SIZE=$((512 * 1024 * 1024))	# bytes
typeset DIO_BS=8k
typeset DIO_JOBS=4
typeset DIO_RUNTIME=20
typeset DBUF_SHIFT_TEST=11
typeset PUMP_FRACTION=8

DIO_FILE=""
SAVED_ARC_MIN=""
SAVED_ARC_MAX=""
SAVED_DBUF_SHIFT=""
SAVED_DIO_ENABLED=""

function cleanup
{
	[[ -n "$DIO_FILE" ]] && rm -f "$DIO_FILE"
	[[ -n "$SAVED_DBUF_SHIFT" ]] && \
	    set_tunable32 DBUF_CACHE_SHIFT "$SAVED_DBUF_SHIFT"
	if tunable_exists DIO_ENABLED; then
		[[ -n "$SAVED_DIO_ENABLED" ]] && \
		    set_tunable32 DIO_ENABLED "$SAVED_DIO_ENABLED"
	fi
	[[ -n "$SAVED_ARC_MAX" ]] && set_tunable64 ARC_MAX "$SAVED_ARC_MAX"
	[[ -n "$SAVED_ARC_MIN" ]] && set_tunable64 ARC_MIN "$SAVED_ARC_MIN"
}

# Fetch an integer kstat value.
function get_uint
{
	typeset val
	val=$(kstat "$1")
	echo "$val" | grep -Eq '^[0-9]+$' ||
	    log_fail "kstat $1 returned non-numeric '$val'"
	echo "$val"
}

# The largest dbuf_cache_extra the kernel can grant right now: its ceiling
# (arc_c_max >> dbuf_cache_extra_max_shift) minus the current dbuf budget
# (arc_c >> dbuf_cache_shift).  dbuf_cache_max_bytes is left at its default
# (unlimited) value by this test, so it is not binding.
function get_extra_ceiling
{
	typeset c=$(get_uint arcstats.c)
	typeset c_max=$(get_uint arcstats.c_max)
	typeset extra_shift=$(get_tunable DBUF_CACHE_EXTRA_MAX_SHIFT)
	typeset base_shift=$(get_tunable DBUF_CACHE_SHIFT)
	echo $(((c_max >> extra_shift) - (c >> base_shift)))
}

log_assert "dbuf_cache_extra rises (or is already at its ceiling) under " \
    "Direct I/O churn and decays after"
log_onexit cleanup

typeset mntpnt
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
DIO_FILE=$mntpnt/dio-cache-extra.$$

# Metadata-only caching keeps the ARC from ever filling with file data, so
# the ARC stays well below its target during the test (the DIO/cache-off
# regime the allowance targets).  Direct I/O itself never caches data.
log_must zfs set recordsize=$RECORDSIZE $TESTPOOL/$TESTFS
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs set primarycache=metadata $TESTPOOL/$TESTFS

# Save and shrink the dbuf budget so a modest file can overflow it.
SAVED_DBUF_SHIFT=$(get_tunable DBUF_CACHE_SHIFT)
log_must set_tunable32 DBUF_CACHE_SHIFT $DBUF_SHIFT_TEST
log_note "dbuf_cache_shift now $(get_tunable DBUF_CACHE_SHIFT)"

if tunable_exists DIO_ENABLED; then
	SAVED_DIO_ENABLED=$(get_tunable DIO_ENABLED)
	log_must set_tunable32 DIO_ENABLED 1
fi

#
# Force the ARC target (arc_c) down so dbuf_cache_extra has headroom up to
# arc_c_max: pin arc_min == arc_max == c_max/8 (which clamps arc_c down to
# that value), let the ARC shrink to it, then restore arc_max.  arc_c stays
# low afterwards because a Direct I/O workload never fills the ARC.
#
# Order matters.  FreeBSD's arc.max handler rejects a value <= arc_c_min,
# while arc.min only has to be <= arc_c_max.  Setting arc_max first makes
# arc_c_max == pump, after which pinning arc_min == pump is accepted.  On
# Linux the order is irrelevant (arc.max is clamped by arc_tuning_update).
#
SAVED_ARC_MIN=$(get_tunable ARC_MIN)
SAVED_ARC_MAX=$(get_tunable ARC_MAX)
typeset c_max=$(get_uint arcstats.c_max)
typeset pump=$((c_max / PUMP_FRACTION))
# arc_max must be at least MIN_ARC_MAX (64 MiB) so it is accepted on FreeBSD.
(( pump < (64 * 1024 * 1024) )) && pump=$((64 * 1024 * 1024))
(( pump >= c_max )) && pump=$((c_max / 2))
log_must set_tunable64 ARC_MAX $pump
log_must set_tunable64 ARC_MIN $pump
sleep 3
log_must set_tunable64 ARC_MAX $c_max
sleep 1

log_note "ARC: size=$(get_uint arcstats.size) c=$(get_uint arcstats.c) " \
    "c_min=$(get_uint arcstats.c_min) c_max=$(get_uint arcstats.c_max)"

# Pre-create a real (non-sparse) file so random Direct I/O has data to read.
log_must fio --name=prep --filename=$DIO_FILE --rw=write \
    --size=$FILE_SIZE --bs=1m --ioengine=sync --direct=0 \
    --fallocate=none --group_reporting --minimal >/dev/null 2>&1
sync_all_pools

typeset base_extra extra_ceiling
base_extra=$(get_uint dbufstats.cache_extra_bytes)
extra_ceiling=$(get_extra_ceiling)
log_note "baseline cache_extra_bytes=$base_extra ceiling=$extra_ceiling"
typeset evicts_before=$(get_uint dbufstats.cache_total_evicts)

#
# Direct I/O random reads: they never cache data, but each one walks the L1
# indirect dbuf that locates the block, churning those dbufs through the
# (intentionally tiny) dbuf LRU cache and driving cache_extra_bytes up.
#
log_must eval "fio --name=dio-randread --filename=$DIO_FILE --rw=randread \
    --size=$FILE_SIZE --bs=$DIO_BS --ioengine=sync --direct=1 --numjobs=$DIO_JOBS \
    --group_reporting --minimal --runtime=$DIO_RUNTIME --time_based \
    --fallocate=none >/dev/null 2>&1 &"

typeset i=0 peak=0 val=0
while (( i < DIO_RUNTIME + 3 )); do
	sleep 1
	val=$(get_uint dbufstats.cache_extra_bytes)
	log_note "t=${i}s cache_extra_bytes=$val " \
	    "cache_size_bytes=$(get_uint dbufstats.cache_size_bytes) " \
	    "cache_target_bytes=$(get_uint dbufstats.cache_target_bytes)"
	(( val > peak )) && peak=$val
	(( i += 1 ))
done
wait
typeset evicts_after=$(get_uint dbufstats.cache_total_evicts)
log_note "cache_extra_bytes: baseline=$base_extra peak=$peak"
log_note "cache_total_evicts: before=$evicts_before after=$evicts_after"

# The allowance must actually have grown, i.e. the mechanism engaged.  The
# allowance is global and sticky, so churn earlier in this test (ARC pump +
# file prep) can already have ramped it to its ceiling.  In that case there is
# no headroom left for Direct I/O to raise it, and we instead verify that the
# Direct I/O churn really is evicting dbufs.
if (( peak <= base_extra )); then
	if (( base_extra < extra_ceiling )); then
		log_fail "cache_extra_bytes did not rise under Direct I/O " \
		    "(baseline=$base_extra peak=$peak ceiling=$extra_ceiling)"
	fi
	log_note "allowance was already at its ceiling ($extra_ceiling); " \
	    "verifying Direct I/O is still churning dbufs"
	(( evicts_after > evicts_before )) || log_fail \
	    "no dbuf evictions during Direct I/O " \
	    "(before=$evicts_before after=$evicts_after)"
fi

#
# Free the file's dbufs and let the once-per-second tick decay the now
# unneeded allowance.  The resident dbuf set collapses on delete, so the
# decay branch halves dbuf_cache_extra toward zero over the following ticks.
#
rm -f "$DIO_FILE"
sync_all_pools

typeset target=$((peak / 2))
i=0
val=$peak
while (( i < 10 )); do
	sleep 1
	val=$(get_uint dbufstats.cache_extra_bytes)
	log_note "decay t=${i}s cache_extra_bytes=$val"
	(( val < target )) && break
	(( i += 1 ))
done
(( val < target )) || log_fail "cache_extra_bytes did not decay after the " \
    "Direct I/O workload stopped (peak=$peak current=$val)"

log_pass "dbuf_cache_extra engaged under Direct I/O and decayed afterwards"
