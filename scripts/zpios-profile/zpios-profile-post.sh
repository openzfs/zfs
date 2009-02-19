#!/bin/bash

PROG=zpios-profile-post.sh

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

if [ "${RUN_PHASE}" != "post" ]; then
        exit 1
fi

. ${RUN_LOG_DIR}/zpios-config.sh
. ${RUN_LOG_DIR}/${RUN_ID}/zpios-config-run.sh

#kill -s SIGHUP `cat ${PROFILE_PID}`
#rm -f ${PROFILE_PID}

OPROFILE_SHORT_ARGS="-a -g -l -p ${KERNEL_BIN},${SPL_BIN},${ZFS_BIN}"
OPROFILE_LONG_ARGS="-d -a -g -l -p ${KERNEL_BIN},${SPL_BIN},${ZFS_BIN}"

/usr/bin/opcontrol --stop >>${OPROFILE_LOG} 2>&1
/usr/bin/opcontrol --dump >>${OPROFILE_LOG} 2>&1
/usr/bin/opreport ${OPROFILE_SHORT_ARGS} >${OPROFILE_SHORT_LOG} 2>&1
/usr/bin/opreport ${OPROFILE_LONG_ARGS} >${OPROFILE_LONG_LOG} 2>&1

if [ -f ${PROFILE_ARC_PROC} ]; then
	cat ${PROFILE_ARC_PROC} >${PROFILE_ARC_LOG}
fi

if [ -f ${PROFILE_VDEV_CACHE_PROC} ]; then
	cat ${PROFILE_VDEV_CACHE_PROC} >${PROFILE_VDEV_CACHE_LOG}
fi

# Summarize system time per pid
${PROFILE_ZPIOS_PIDS_BIN} ${RUN_LOG_DIR} ${RUN_ID} >${PROFILE_ZPIOS_PIDS_LOG}

# Summarize per device performance
${PROFILE_ZPIOS_DISK_BIN} ${RUN_LOG_DIR} ${RUN_ID} >${PROFILE_ZPIOS_DISK_LOG}

exit 0
