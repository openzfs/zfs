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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

function cleanup
{
	# kill fio and iostat
	pkill fio
	pkill iostat

	#
	# We're using many filesystems depending on the number of
	# threads for each test, and there's no good way to get a list
	# of all the filesystems that should be destroyed on cleanup
	# (i.e. the list of filesystems used for the last test ran).
	# Thus, we simply recreate the pool as a way to destroy all
	# filesystems and leave a fresh pool behind.
	#
	recreate_perf_pool
}

trap "log_fail \"Measure IO stats during random write load\"" SIGTERM
log_onexit cleanup

recreate_perf_pool

# Aim to fill the pool to 50% capacity while accounting for a 3x compressratio.
export TOTAL_SIZE=$(($(get_prop avail $PERFPOOL) * 3 / 2))

if [[ -n $PERF_REGRESSION_WEEKLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_WEEKLY}
	export PERF_RANDSEED=${PERF_RANDSEED:-'1234'}
	export PERF_COMPPERCENT=${PERF_COMPPERCENT:-'66'}
	export PERF_COMPCHUNK=${PERF_COMPCHUNK:-'4096'}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'weekly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'1 2 4 8 16 32 64 128'}
	export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0 1'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'8k'}

elif [[ -n $PERF_REGRESSION_NIGHTLY ]]; then
	export PERF_RUNTIME=${PERF_RUNTIME:-$PERF_RUNTIME_NIGHTLY}
	export PERF_RANDSEED=${PERF_RANDSEED:-'1234'}
	export PERF_COMPPERCENT=${PERF_COMPPERCENT:-'66'}
	export PERF_COMPCHUNK=${PERF_COMPCHUNK:-'4096'}
	export PERF_RUNTYPE=${PERF_RUNTYPE:-'nightly'}
	export PERF_NTHREADS=${PERF_NTHREADS:-'1 4 16 64'}
	export PERF_NTHREADS_PER_FS=${PERF_NTHREADS_PER_FS:-'0 1'}
	export PERF_SYNC_TYPES=${PERF_SYNC_TYPES:-'1'}
	export PERF_IOSIZES=${PERF_IOSIZES:-'8k'}
fi

# Until the performance tests over NFS can deal with multiple file systems,
# force the use of only one file system when testing over NFS.
[[ $NFS -eq 1 ]] && PERF_NTHREADS_PER_FS='0'

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
	    "kstat zfs:0 1" "kstat"
	    "vmstat -T d 1" "vmstat"
	    "mpstat -T d 1" "mpstat"
	    "iostat -T d -xcnz 1" "iostat"
	    "dtrace -Cs $PERF_SCRIPTS/io.d $PERFPOOL $lun_list 1" "io"
	    "dtrace  -s $PERF_SCRIPTS/zil.d $PERFPOOL 1" "zil"
	    "dtrace  -s $PERF_SCRIPTS/profile.d" "profile"
	    "dtrace  -s $PERF_SCRIPTS/offcpu-profile.d" "offcpu-profile"
	)
fi
log_note "ZIL specific random write workload with $PERF_RUNTYPE settings"
do_fio_run random_writes.fio true false
log_pass "Measure IO stats during ZIL specific random write workload"
