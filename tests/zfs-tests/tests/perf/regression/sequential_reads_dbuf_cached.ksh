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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2016, 2021 by Delphix. All rights reserved.
#

#
# Description:
# Trigger fio runs using the sequential_reads job file. The number of runs and
# data collected is determined by the PERF_* variables. See do_fio_run for
# details about these variables.
#
# The files to read from are created prior to the first fio run, and used
# for all fio runs. The ARC is not cleared to ensure that all data is cached.
#
# This is basically a copy of the sequential_reads_cached test case, but with
# a smaller dataset so that we can fit everything into the decompressed, linear
# space in the dbuf cache.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

command -v fio > /dev/null || log_unsupported "fio missing"

function cleanup
{
	# kill fio and iostat
	pkill fio
	pkill iostat
	recreate_perf_pool
}

trap "log_fail \"Measure IO stats during sequential read load\"" SIGTERM
log_onexit cleanup

recreate_perf_pool
populate_perf_filesystems

# Ensure the working set can be cached in the dbuf cache.
export TOTAL_SIZE=$(($(get_dbuf_cache_size) * 3 / 4))

# Variables specific to this test for use by fio.
export PERF_NTHREADS=${PERF_NTHREADS:-'64'}
export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
export PERF_IOSIZES=${PERF_IOSIZES:-'64k'}
export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}

# Layout the files to be used by the read tests. Create as many files as the
# largest number of threads. An fio run with fewer threads will use a subset
# of the available files.
export NUMJOBS=$(get_max $PERF_NTHREADS)
export FILE_SIZE=$((TOTAL_SIZE / NUMJOBS))
export DIRECTORY=$(get_directory)
log_must fio $FIO_SCRIPTS/mkfiles.fio

# Set up the scripts and output files that will log performance data.
lun_list=$(pool_to_lun_list $PERFPOOL)
log_note "Collecting backend IO stats with lun list $lun_list"
if is_linux; then
	typeset perf_record_cmd="perf record -F 99 -a -g -q \
	    -o /dev/stdout -- sleep ${PERF_RUNTIME}"

	export collect_scripts=(
	    "zpool iostat -lpvyL $PERFPOOL 1" "zpool.iostat"
	    "$PERF_SCRIPTS/prefetch_io.sh $PERFPOOL 1" "prefetch"
	    "vmstat -t 1" "vmstat"
	    "mpstat -P ALL 1" "mpstat"
	    "iostat -tdxyz 1" "iostat"
	    "$perf_record_cmd" "perf"
	)
else
	export collect_scripts=(
	    "kstat zfs:0 1" "kstat"
	    "vmstat -T d 1" "vmstat"
	    "mpstat -T d 1" "mpstat"
	    "iostat -T d -xcnz 1" "iostat"
	    "dtrace -Cs $PERF_SCRIPTS/io.d $PERFPOOL $lun_list 1" "io"
	    "dtrace -Cs $PERF_SCRIPTS/prefetch_io.d $PERFPOOL 1" "prefetch"
	    "dtrace -s $PERF_SCRIPTS/profile.d" "profile"
	)
fi

log_note "Sequential cached reads with settings: $(print_perf_settings)"
do_fio_run sequential_reads.fio false false
log_pass "Measure IO stats during sequential cached read load"
