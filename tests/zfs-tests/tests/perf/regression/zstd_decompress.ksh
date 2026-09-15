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
# Prepare zstd-compressed files, then measure reads that must decompress them.
# This is separate from the compression baseline because writes do not exercise
# initialized decompression-context reuse.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib
. $STF_SUITE/tests/perf/regression/zstd.shlib

command -v fio > /dev/null || log_unsupported "fio missing"

function cleanup
{
	clear_zinject_delays
	if [[ -n $zstd_cache_max_before ]]; then
		set_tunable32 ZSTD_CACHE_MAX $zstd_cache_max_before
	fi
	if poolexists "$PERFPOOL"; then
		destroy_pool "$PERFPOOL"
	fi
}

trap "log_fail \"Measure zstd decompression\"" SIGTERM
log_onexit cleanup

typeset zstd_level=${PERF_ZSTD_LEVEL:-3}
typeset zstd_runtime=${PERF_ZSTD_RUNTIME:-30}
typeset zstd_cache_max=${PERF_ZSTD_CACHE_MAX:-}
typeset zstd_cache_max_before
typeset zstd_cache_max_effective

export PERF_RUNTIME=$zstd_runtime
export PERF_NTHREADS=${PERF_NTHREADS:-'1'}
export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
export PERF_IOSIZES=${PERF_IOSIZES:-'128k'}
export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'0'}
export PERF_FS_OPTS="-o recsize=128k -o compress=zstd-$zstd_level \
    -o checksum=sha256 -o redundant_metadata=most"

recreate_perf_pool
populate_perf_filesystems

if [[ -n $zstd_cache_max ]]; then
	zstd_cache_max_before=$(get_tunable ZSTD_CACHE_MAX)
	log_must set_tunable32 ZSTD_CACHE_MAX $zstd_cache_max
fi
zstd_cache_max_effective=$(get_tunable ZSTD_CACHE_MAX)

# Prepare enough fixed-size files for the largest read-run concurrency. Keep
# the logical workload below the uncompressed pool capacity so preparation does
# not depend on the codec's compression ratio.
typeset threads=$(get_max $PERF_NTHREADS)
TOTAL_SIZE=$(get_zstd_workload_size \
	"$(get_prop avail "$PERFPOOL")" "$PERF_COMPPERCENT") || \
	log_fail "Invalid PERF_COMPPERCENT: $PERF_COMPPERCENT"
export TOTAL_SIZE
export NUMJOBS=$threads
(( FILE_SIZE = TOTAL_SIZE / threads ))
export FILE_SIZE
export DIRECTORY=$(get_directory)
export SYNC_TYPE=0
export BLOCKSIZE=128k
export DIRECT=0
log_must fio --output-format="${PERF_FIO_FORMAT:-json}" \
	--output /dev/null "$FIO_SCRIPTS/mkfiles.fio"

# Warm the compressed blocks into the ARC before starting collectors. This
# dedicated job has no runtime limit and completes one pass over each file.
log_must fio --output-format="${PERF_FIO_FORMAT:-json}" \
	--output /dev/null "$FIO_SCRIPTS/zstd_warmup.fio"

if is_linux; then
	[[ -r /proc/spl/kstat/zfs/zstd ]] || \
	    log_unsupported "Linux zstd kstat is unavailable"

	export collect_scripts=(
	    "$PERF_SCRIPTS/zstd_iostat.sh" "zpool.iostat"
	    "$PERF_SCRIPTS/zstd_kstat.sh" "zstd.kstat"
	    "$PERF_SCRIPTS/zstd_vmstat.sh" "vmstat"
	)
	if command -v perf > /dev/null; then
		export PERF_COLLECT_OPTIONAL_SCRIPTS="$PERF_SCRIPTS/zstd_perf.sh"
		collect_scripts+=("$PERF_SCRIPTS/zstd_perf.sh" "perf")
	else
		log_note "perf missing; skipping optional profiling"
	fi
else
	export collect_scripts=(
	    "$PERF_SCRIPTS/zstd_kstat.sh" "zstd.kstat"
	    "$PERF_SCRIPTS/zstd_vmstat.sh" "vmstat"
	)
fi

log_note "Zstd decompression with settings: $(print_perf_settings)"
log_note "Zstd level: $zstd_level"
typeset data_misses_before=$(kstat arcstats.demand_data_misses)
typeset metadata_misses_before=$(kstat arcstats.demand_metadata_misses)
typeset context_create_before=$(kstat zstd.decompress_context_create)
typeset context_reuse_before=$(kstat zstd.decompress_context_reuse)
do_fio_run sequential_reads.fio false false
typeset data_misses_after=$(kstat arcstats.demand_data_misses)
typeset metadata_misses_after=$(kstat arcstats.demand_metadata_misses)
typeset context_create_after=$(kstat zstd.decompress_context_create)
typeset context_reuse_after=$(kstat zstd.decompress_context_reuse)
(( data_misses_after == data_misses_before )) || \
	log_fail "cached zstd benchmark incurred demand data misses"
(( metadata_misses_after == metadata_misses_before )) || \
	log_fail "cached zstd benchmark incurred demand metadata misses"
if [[ $zstd_cache_max_effective == 0 ]]; then
	(( context_create_after > context_create_before )) || \
		log_fail "uncached zstd benchmark did not create decompression contexts"
	(( context_reuse_after == context_reuse_before )) || \
		log_fail "uncached zstd benchmark unexpectedly reused a context"
else
	(( context_reuse_after > context_reuse_before )) || \
		log_fail "cached zstd benchmark did not reuse a decompression context"
fi

if [[ -n $zstd_cache_max_before ]]; then
	log_must set_tunable32 ZSTD_CACHE_MAX $zstd_cache_max_before
fi
log_pass "Measure zstd decompression"
