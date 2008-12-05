#!/bin/bash

prog=profile-zpios-post.sh
. ../.script-config

RUN_POST=${0}
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

PROFILE_ZPIOS_PIDS_BIN=/home/behlendo/src/zfs/scripts/profile-zpios-pids.sh
PROFILE_ZPIOS_PIDS_LOG=${RUN_LOG_DIR}/${RUN_ID}/pids-summary.csv

PROFILE_ZPIOS_DISK_BIN=/home/behlendo/src/zfs/scripts/profile-zpios-disk.sh
PROFILE_ZPIOS_DISK_LOG=${RUN_LOG_DIR}/${RUN_ID}/disk-summary.csv

PROFILE_ZPIOS_ARC_LOG=${RUN_LOG_DIR}/${RUN_ID}/arcstats
PROFILE_ZPIOS_VDEV_LOG=${RUN_LOG_DIR}/${RUN_ID}/vdev_cache_stats

KERNEL_BIN="/lib/modules/`uname -r`/kernel/"
SPL_BIN="${SPLBUILD}/modules/spl/"
ZFS_BIN="${ZFSBUILD}/lib/"

OPROFILE_SHORT_ARGS="-a -g -l -p ${KERNEL_BIN},${SPL_BIN},${ZFS_BIN}"
OPROFILE_LONG_ARGS="-d -a -g -l -p ${KERNEL_BIN},${SPL_BIN},${ZFS_BIN}"

OPROFILE_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile.txt
OPROFILE_SHORT_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile-short.txt
OPROFILE_LONG_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile-long.txt
PROFILE_PID=${RUN_LOG_DIR}/${RUN_ID}/pid

if [ "${RUN_PHASE}" != "post" ]; then
        exit 1
fi

# opcontrol --stop >>${OPROFILE_LOG} 2>&1
# opcontrol --dump >>${OPROFILE_LOG} 2>&1

kill -s SIGHUP `cat ${PROFILE_PID}`
rm -f ${PROFILE_PID}

# opreport ${OPROFILE_SHORT_ARGS} >${OPROFILE_SHORT_LOG} 2>&1
# opreport ${OPROFILE_LONG_ARGS} >${OPROFILE_LONG_LOG} 2>&1

# opcontrol --deinit >>${OPROFILE_LOG} 2>&1

cat /proc/spl/kstat/zfs/arcstats >${PROFILE_ZPIOS_ARC_LOG}
cat /proc/spl/kstat/zfs/vdev_cache_stats >${PROFILE_ZPIOS_VDEV_LOG}

# Summarize system time per pid
${PROFILE_ZPIOS_PIDS_BIN} ${RUN_LOG_DIR} ${RUN_ID} >${PROFILE_ZPIOS_PIDS_LOG}

# Summarize per device performance
${PROFILE_ZPIOS_DISK_BIN} ${RUN_LOG_DIR} ${RUN_ID} >${PROFILE_ZPIOS_DISK_LOG}

exit 0
