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

# shellcheck disable=SC2154

. "$STF_SUITE/include/libtest.shlib"

verify_runnable "both"

typeset prefix="$TESTDIR/zstd-cctx-cache"
typeset expected="$prefix.expected"
typeset -a pids

function cleanup
{
	for pid in "${pids[@]}"; do
		kill -TERM "$pid" 2>/dev/null || :
	done
	for pid in "${pids[@]}"; do
		wait "$pid" 2>/dev/null || :
	done
	zfs set primarycache=all "${TESTPOOL}/${TESTFS}"
	rm -f "$prefix".*
}

log_assert "Initialized zstd compression contexts remain correct on reuse"
log_onexit cleanup

log_must zfs set compression=zstd-1 "${TESTPOOL}/${TESTFS}"
log_must zfs set recordsize=128K "${TESTPOOL}/${TESTFS}"
log_must zfs set primarycache=metadata "${TESTPOOL}/${TESTFS}"

# Repeated random halves match beyond LZ4's 64 KiB window, so the LZ4 early
# abort rejects the block while Zstd can compress it.  Prime the cache, then
# check reuse across blocks at zstd-1 and the early-abort path at zstd-3.
log_must dd if=/dev/urandom of="$expected" bs=64K count=1
log_must dd if="$expected" of="$expected" bs=64K count=1 seek=1 \
	conv=notrunc
log_must sync_all_pools
for level in 1 3; do
	log_must zfs set compression=zstd-$level "${TESTPOOL}/${TESTFS}"
	typeset reuse_before_writes
	reuse_before_writes=$(kstat zstd.compress_context_reuse) || \
		log_fail "could not read zstd context reuse counter"
	for i in $(seq 0 15); do
		(
			dd if="$expected" of="$prefix.$level.$i" bs=128K count=1
		) &
		pids+=($!)
	done

	typeset writers_failed=0
	for pid in "${pids[@]}"; do
		wait "$pid" || writers_failed=1
	done
	pids=()
	(( writers_failed == 0 )) || log_fail "concurrent zstd writer failed"
	log_must sync_all_pools

	for i in $(seq 0 15); do
		log_must cmp "$expected" "$prefix.$level.$i"
		typeset blocks
		blocks=$(stat_blocks "$prefix.$level.$i") || \
		    log_fail "could not read allocated blocks for writer $i"
		(( blocks > 0 && blocks * 512 < 128 * 1024 )) || \
		    log_fail "zstd-$level writer $i was not stored compressed"
	done

	typeset reuse_after_writes
	reuse_after_writes=$(kstat zstd.compress_context_reuse) || \
		log_fail "could not read zstd context reuse counter"
	log_note "zstd-$level compression context reuse count: " \
		"${reuse_before_writes} -> ${reuse_after_writes}"
	(( reuse_after_writes > reuse_before_writes )) || \
		log_fail "zstd-$level writes did not reuse compression contexts"
done

log_pass "Concurrent zstd writes reused contexts and preserved file contents"
