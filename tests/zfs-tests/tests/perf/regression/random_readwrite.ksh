#!/bin/ksh

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
# Copyright (c) 2015, 2021 by Delphix. All rights reserved.
#

#
# Description:
# Trigger fio runs using the random_readwrite job file. The number of runs and
# data collected is determined by the PERF_* variables. See do_fio_run for
# details about these variables.
#
# The files to read and write from are created prior to the first fio run,
# and used for all fio runs. The ARC is cleared with `zinject -a` prior to
# each run so reads will go to disk.
#
# Thread/Concurrency settings:
#    PERF_NTHREADS defines the number of files created in the test filesystem,
#    as well as the number of threads that will simultaneously drive IO to
#    those files.  The settings chosen are from measurements in the
#    PerfAutoESX/ZFSPerfESX Environments, selected at concurrency levels that
#    are at peak throughput but lowest latency.  Higher concurrency introduces
#    queue time latency and would reduce the impact of code-induced performance
#    regressions.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

function cleanup
{
	# kill fio and iostat
	pkill fio
	pkill iostat
	recreate_perf_pool
}

trap "log_fail \"Measure IO stats during random read load\"" SIGTERM
log_onexit cleanup

recreate_perf_pool
populate_perf_filesystems

# Aim to fill the pool to 50% capacity while accounting for a 3x compressratio.
export TOTAL_SIZE=$(($(get_prop avail $PERFPOOL) * 3 / 2))

# Variables specific to this test for use by fio.
export PERF_NTHREADS=${PERF_NTHREADS:-'32 64'}
export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
export PERF_IOSIZES=''		# bssplit used instead

# Layout the files to be used by the readwrite tests. Create as many files
# as the largest number of threads. An fio run with fewer threads will use
# a subset of the available files.
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
	    "vmstat -t 1" "vmstat"
	    "mpstat -P ALL 1" "mpstat"
	    "iostat -tdxyz 1" "iostat"
	    "$perf_record_cmd" "perf"
	)
else
	export collect_scripts=(
	    "$PERF_SCRIPTS/io.d $PERFPOOL $lun_list 1" "io"
	    "vmstat -T d 1" "vmstat"
	    "mpstat -T d 1" "mpstat"
	    "iostat -T d -xcnz 1" "iostat"
	)
fi

log_note "Random reads and writes with settings: $(print_perf_settings)"
do_fio_run random_readwrite.fio false true
log_pass "Measure IO stats during random read and write load"
