#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License (CDDL), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Description:
# Run a fixed 128 KiB zstd compression workload through the existing kernel
# performance harness while collecting zstd kstat snapshots.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

command -v fio > /dev/null || log_unsupported "fio missing"

function cleanup
{
	recreate_perf_pool
}

trap "log_fail \"Measure zstd compression lifecycle baseline\"" SIGTERM
log_onexit cleanup

typeset zstd_level=${PERF_ZSTD_LEVEL:-3}
typeset zstd_runtime=${PERF_ZSTD_RUNTIME:-30}

export PERF_RUNTIME=$zstd_runtime
export PERF_NTHREADS=${PERF_NTHREADS:-'1'}
export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
export PERF_IOSIZES=${PERF_IOSIZES:-'128k'}
export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'0'}
export PERF_FS_OPTS="-o recsize=128k -o compress=zstd-$zstd_level \
    -o checksum=sha256 -o redundant_metadata=most"

recreate_perf_pool

typeset comp_percent=$PERF_COMPPERCENT
TOTAL_SIZE=$(get_zstd_workload_size "$(get_prop avail "$PERFPOOL")" \
	"$comp_percent") || log_fail "Invalid PERF_COMPPERCENT: $comp_percent"
export TOTAL_SIZE

if is_linux; then
	[[ -r /proc/spl/kstat/zfs/zstd ]] || \
	    log_unsupported "Linux zstd kstat is unavailable"

	export collect_scripts=(
	    "$PERF_SCRIPTS/zstd_iostat.sh" "zpool.iostat"
	    "$PERF_SCRIPTS/zstd_kstat.sh" "zstd.kstat"
	    "$PERF_SCRIPTS/zstd_vmstat.sh" "vmstat"
	)
	if command -v perf > /dev/null; then
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

log_note "Zstd compression with settings: $(print_perf_settings)"
log_note "Zstd level: $zstd_level"
do_fio_run sequential_writes.fio true false
log_pass "Measure zstd compression lifecycle baseline"
