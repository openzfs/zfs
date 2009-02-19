#!/bin/bash

PROG=zpios-profile-pre.sh

PROFILE_ZPIOS_READY=0
trap "PROFILE_ZPIOS_READY=1" SIGHUP

RUN_PRE=${0}
RUN_PHASE=${1}
RUN_LOG_DIR=${2}
RUN_ID=${3}
RUN_POOL=${4}
RUN_CHUNK_SIZE=${5}
RUN_REGION_SIZE=${6}
RUN_THREAD_COUNT=${7}
RUN_REGION_COUNT=${8}
RUN_OFFSET=${9}
RUN_REGION_NOISE=${10}
RUN_CHUNK_NOISE=${11}
RUN_THREAD_DELAY=${12}
RUN_FLAGS=${13}
RUN_RESULT=${14}

zpios_profile_run_cfg() {
cat > ${RUN_LOG_DIR}/${RUN_ID}/zpios-config-run.sh << EOF
#
# Zpios Profiling Configuration for Run ${RUN_ID}
#

PROFILE_PID=${RUN_LOG_DIR}/${RUN_ID}/profile.pid

PROFILE_ZPIOS_PIDS_LOG=${RUN_LOG_DIR}/${RUN_ID}/pids-summary.csv
PROFILE_ZPIOS_DISK_LOG=${RUN_LOG_DIR}/${RUN_ID}/disk-summary.csv

PROFILE_ARC_LOG=${RUN_LOG_DIR}/${RUN_ID}/arcstats
PROFILE_ARC_PROC=/proc/spl/kstat/zfs/arcstats

PROFILE_VDEV_CACHE_LOG=${RUN_LOG_DIR}/${RUN_ID}/vdev_cache_stats
PROFILE_VDEV_CACHE_PROC=/proc/spl/kstat/zfs/vdev_cache_stats

OPROFILE_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile.txt
OPROFILE_SHORT_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile-short.txt
OPROFILE_LONG_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile-long.txt

EOF
}

zpios_profile_run_args() {
cat > ${RUN_LOG_DIR}/${RUN_ID}/zpios-args.txt << EOF
#
# Zpios Arguments for Run ${RUN_ID}
#

LOG_DIR=${RUN_LOG_DIR}
ID=${RUN_ID}
POOL=${RUN_POOL}
CHUNK_SIZE=${RUN_CHUNK_SIZE}
REGION_SIZE=${RUN_REGION_SIZE}
THREAD_COUNT=${RUN_THREAD_COUNT}
REGION_COUNT=${RUN_REGION_COUNT}
OFFSET=${RUN_OFFSET}
REGION_NOISE=${RUN_REGION_NOISE}
CHUNK_NOISE=${RUN_CHUNK_NOISE}
THREAD_DELAY=${RUN_THREAD_DELAY}
FLAGS=${RUN_FLAGS}
RESULT=${RUN_RESULT}
EOF
}

if [ "${RUN_PHASE}" != "pre" ]; then
        exit 1
fi

mkdir -p ${RUN_LOG_DIR}/${RUN_ID}/
zpios_profile_run_cfg
zpios_profile_run_args

. ${RUN_LOG_DIR}/zpios-config.sh
. ${RUN_LOG_DIR}/${RUN_ID}/zpios-config-run.sh

# Start the profile script
#${PROFILE_ZPIOS_BIN} ${RUN_PHASE} ${RUN_LOG_DIR} ${RUN_ID} &
#echo "$!" >${PROFILE_PID}

# Sleep waiting for profile script to be ready, it will
# signal us via SIGHUP when it is ready to start profiling.
#while [ ${PROFILE_ZPIOS_READY} -eq 0 ]; do
#	sleep 0.1
#done

/usr/bin/opcontrol --reset >>${OPROFILE_LOG} 2>&1
/usr/bin/opcontrol --start >>${OPROFILE_LOG} 2>&1

exit 0
