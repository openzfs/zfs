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

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

typeset zstd_cache_file="$TESTDIR/zstd-dctx-cache"
typeset zstd_cache_expected="${TMPDIR:-/tmp}/zstd-dctx-cache.expected.$$"
typeset zstd_cache_actual="${TMPDIR:-/tmp}/zstd-dctx-cache.actual.$$"
typeset zstd_cache_slice_prefix="${TMPDIR:-/tmp}/zstd-dctx-cache.slice.$$"
typeset zstd_cache_max

function cleanup
{
	log_must zinject -c all
	if [[ -n $zstd_cache_max ]]; then
		log_must set_tunable32 ZSTD_CACHE_MAX $zstd_cache_max
	fi
	zfs set primarycache=all $TESTPOOL/$TESTFS
	rm -f "$zstd_cache_file" "$zstd_cache_expected" "$zstd_cache_actual" \
	    "$zstd_cache_slice_prefix".*
}

function verify_read
{
	log_must dd if="$zstd_cache_file" of="$zstd_cache_actual" bs=128K
	log_must cmp "$zstd_cache_expected" "$zstd_cache_actual"
}

log_assert "Concurrent zstd reads reuse initialized decompression contexts"
log_onexit cleanup

zstd_cache_max=$(get_tunable ZSTD_CACHE_MAX)
log_must zfs set compression=zstd-3 $TESTPOOL/$TESTFS
log_must zfs set recordsize=128K $TESTPOOL/$TESTFS
log_must zfs set primarycache=metadata $TESTPOOL/$TESTFS
log_must zstd_dctx_test
log_must file_write -o create -f "$zstd_cache_expected" -b $((128 * 1024)) \
	-c 1 -d 0
typeset pattern
for i in $(seq 1 4095); do
	(( pattern = i % 256 ))
	log_must file_write -o append -f "$zstd_cache_expected" \
		-b $((128 * 1024)) -c 1 -d "$pattern"
done
log_must cp "$zstd_cache_expected" "$zstd_cache_file"
log_must sync

# This serial injected fault covers the ZFS post-decompression error path. It
# verifies that a later valid read succeeds and that reuse remains active; the
# global counter does not identify the context used by either operation. A
# malformed-frame test covers the ZSTD decoder error path separately.
log_must zinject -a
verify_read
typeset reuse_before_failure=$(kstat zstd.decompress_context_reuse)
log_must zinject -a -t data -e decompress -f 100 \
	"$zstd_cache_file"
log_mustnot dd if="$zstd_cache_file" of=/dev/null bs=128K
log_must zinject -c all
verify_read
typeset reuse_after_failure=$(kstat zstd.decompress_context_reuse)
(( reuse_after_failure > reuse_before_failure )) || \
	log_fail "failed read did not reuse its released context"

typeset -a pids
typeset records_per_reader=128
typeset reader_bytes
(( reader_bytes = records_per_reader * 128 * 1024 ))
for i in $(seq 0 31); do
	(( start = i * records_per_reader ))
	typeset expected_slice="$zstd_cache_slice_prefix.$i.expected"
	typeset actual_slice="$zstd_cache_slice_prefix.$i.actual"
	(
		dd if="$zstd_cache_expected" of="$expected_slice" bs=128K \
			skip="$start" count="$records_per_reader" 2>/dev/null || exit 1
		dd if="$zstd_cache_file" of="$actual_slice" bs=128K \
			skip="$start" count="$records_per_reader" 2>/dev/null || exit 1
		[ "$(wc -c < "$expected_slice")" -eq "$reader_bytes" ] || exit 1
		[ "$(wc -c < "$actual_slice")" -eq "$reader_bytes" ] || exit 1
		cmp "$expected_slice" "$actual_slice"
	) &
	pids+=($!)
done
for pid in ${pids[*]}; do
	log_must wait $pid
done

log_must zinject -a
verify_read

# Disable the initialized-context cache to exercise the uncached fallback
# deterministically, without relying on enough concurrent work to occupy all
# slots at once.
typeset create_before_fallback=$(kstat zstd.decompress_context_create)
log_must set_tunable32 ZSTD_CACHE_MAX 0
verify_read
typeset create_after_fallback=$(kstat zstd.decompress_context_create)
(( create_after_fallback > create_before_fallback )) || \
	log_fail "uncached decompression fallback was not exercised"
log_must set_tunable32 ZSTD_CACHE_MAX $zstd_cache_max

log_pass "Concurrent zstd reads reused initialized decompression contexts"
