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
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

#
# Description:
# Trigger fio runs using the random_writes job file. The number of runs and
# data collected is determined by the PERF_* variables. See do_fio_run for
# details about these variables.
#
# Prior to each fio run the dataset is recreated, and fio writes new files
# into an otherwise empty pool.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

function cleanup
{
	# kill fio and iostat
	pkill ${fio##*/}
	pkill ${iostat##*/}
	log_must_busy zfs destroy $TESTFS
	log_must_busy zpool destroy $PERFPOOL
}

trap "log_fail \"Measure IO stats during random read load\"" SIGTERM

log_assert "Measure IO stats during random write load"
log_onexit cleanup

export TESTFS=$PERFPOOL/testfs
recreate_perfpool
log_must zfs create $PERF_FS_OPTS $TESTFS

# Aim to fill the pool to 50% capacity while accounting for a 3x compressratio.
export TOTAL_SIZE=$(($(get_prop avail $TESTFS) * 3 / 2))

# Variables for use by fio.
if [[ -n $PERF_REGRESSION_WEEKLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_WEEKLY}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'weekly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'8 16 64'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'0 1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'8k'}
elif [[ -n $PERF_REGRESSION_NIGHTLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_NIGHTLY}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'nightly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'64 128'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'8k'}
fi

# Set up the scripts and output files that will log performance data.
lun_list=$(pool_to_lun_list $PERFPOOL)
log_note "Collecting backend IO stats with lun list $lun_list"
if is_linux; then
	export collect_scripts=("zpool iostat -lpvyL $PERFPOOL 1" "zpool.iostat"
	    "vmstat 1" "vmstat" "mpstat -P ALL 1" "mpstat" "iostat -dxyz 1"
	    "iostat")
else
	export collect_scripts=("$PERF_SCRIPTS/io.d $PERFPOOL $lun_list 1" "io"
	    "vmstat 1" "vmstat" "mpstat 1" "mpstat" "iostat -xcnz 1" "iostat")
fi

log_note "Random writes with $PERF_RUNTYPE settings"
do_fio_run random_writes.fio true false
log_pass "Measure IO stats during random write load"
