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
# Copyright (c) 2015, 2020 by Delphix. All rights reserved.
#

#
# Description:
# Trigger fio runs using the sequential_reads job file. The number of runs and
# data collected is determined by the PERF_* variables. See do_fio_run for
# details about these variables.
#
# The files to read from are created prior to the first fio run, and used
# for all fio runs. This test will exercise cached read performance from
# a clone filesystem. The data is initially cached in the ARC and then
# a snapshot and clone are created. All the performance runs are then
# initiated against the clone filesystem to exercise the performance of
# reads when the ARC has to create another buffer from a different dataset.
# It will also exercise the need to evict the duplicate buffer once the last
# reference on that buffer is released.
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

# Make sure the working set can be cached in the arc. Aim for 1/2 of arc.
export TOTAL_SIZE=$(($(get_max_arc_size) / 2))

# Variables for use by fio.
if [[ -n $PERF_REGRESSION_WEEKLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_WEEKLY}
	export PERF_RANDSEED=${PERF_RANDSEED:-'1234'}
	export PERF_COMPPERCENT=${PERF_COMPPERCENT:-'66'}
	export PERF_COMPCHUNK=${PERF_COMPCHUNK:-'4096'}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'weekly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'8 16 32 64'}
	export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'8k 64k 128k'}
elif [[ -n $PERF_REGRESSION_NIGHTLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_NIGHTLY}
	export PERF_RANDSEED=${PERF_RANDSEED:-'1234'}
	export PERF_COMPPERCENT=${PERF_COMPPERCENT:-'66'}
	export PERF_COMPCHUNK=${PERF_COMPCHUNK:-'4096'}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'nightly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'64 128'}
	export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'128k 1m'}
fi

# Layout the files to be used by the read tests. Create as many files as the
# largest number of threads. An fio run with fewer threads will use a subset
# of the available files.
export NUMJOBS=$(get_max $PERF_NTHREADS)
export FILE_SIZE=$((TOTAL_SIZE / NUMJOBS))
export DIRECTORY=$(get_directory)
log_must fio $FIO_SCRIPTS/mkfiles.fio

#
# Only a single filesystem is used by this test. To be defensive, we
# double check that TESTFS only contains a single filesystem. We
# wouldn't want to assume this was the case, and have it actually
# contain multiple filesystem (causing cascading failures later).
#
log_must test $(get_nfilesystems) -eq 1

log_note "Creating snapshot, $TESTSNAP, of $TESTFS"
create_snapshot $TESTFS $TESTSNAP
log_note "Creating clone, $PERFPOOL/$TESTCLONE, from $TESTFS@$TESTSNAP"
create_clone $TESTFS@$TESTSNAP $PERFPOOL/$TESTCLONE

#
# We want to run FIO against the clone we created above, and not the
# clone's originating filesystem. Thus, we override the default behavior
# and explicitly set TESTFS to the clone.
#
export TESTFS=$PERFPOOL/$TESTCLONE

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
	    "$PERF_SCRIPTS/io.d $PERFPOOL $lun_list 1" "io"
	    "$PERF_SCRIPTS/prefetch_io.d $PERFPOOL 1" "prefetch"
	    "vmstat -T d 1" "vmstat"
	    "mpstat -T d 1" "mpstat"
	    "iostat -T d -xcnz 1" "iostat"
	)
fi

log_note "Sequential cached reads from $DIRECTORY with $PERF_RUNTYPE settings"
do_fio_run sequential_reads.fio false false
log_pass "Measure IO stats during sequential cached read load"
