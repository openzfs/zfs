#!/bin/bash

PROG=zpios-profile-post.sh

RUN_POST=${0}
RUN_PHASE=${1}
RUN_DIR=${2}
RUN_ID=${3}
RUN_POOL=${4}
RUN_CHUNK_SIZE=${5}
RUN_REGION_SIZE=${6}
RUN_THRD_COUNT=${7}
RUN_REGION_COUNT=${8}
RUN_OFFSET=${9}
RUN_REGION_NOISE=${10}
RUN_CHUNK_NOISE=${11}
RUN_THRD_DELAY=${12}
RUN_FLAGS=${13}
RUN_RESULT=${14}

# Summarize system time per process
zpios_profile_post_pids() {
	${PROFILE_PIDS} ${PROFILE_RUN_CR_PIDS_LOG} >${PROFILE_RUN_CR_PIDS_CSV}
	${PROFILE_PIDS} ${PROFILE_RUN_WR_PIDS_LOG} >${PROFILE_RUN_WR_PIDS_CSV}
	${PROFILE_PIDS} ${PROFILE_RUN_RD_PIDS_LOG} >${PROFILE_RUN_RD_PIDS_CSV}
	${PROFILE_PIDS} ${PROFILE_RUN_RM_PIDS_LOG} >${PROFILE_RUN_RM_PIDS_CSV}
}

zpios_profile_post_disk() {
	${PROFILE_DISK} ${PROFILE_RUN_CR_DISK_LOG} >${PROFILE_RUN_CR_DISK_CSV}
	${PROFILE_DISK} ${PROFILE_RUN_WR_DISK_LOG} >${PROFILE_RUN_WR_DISK_CSV}
	${PROFILE_DISK} ${PROFILE_RUN_RD_DISK_LOG} >${PROFILE_RUN_RD_DISK_CSV}
	${PROFILE_DISK} ${PROFILE_RUN_RM_DISK_LOG} >${PROFILE_RUN_RM_DISK_CSV}
}

# Summarize per device performance

# Stop a user defined profiling script which is gathering additional data
zpios_profile_post_stop() {
	local PROFILE_PID=$1

	kill -s SIGHUP `cat ${PROFILE_PID}`


	# Sleep waiting for profile script to exit
	while [ -f ${PROFILE_PID} ]; do
		sleep 0.01
	done
}

zpios_profile_post_proc_stop() {
	local PROC_DIR=$1

	if [ -f ${PROFILE_ARC_PROC} ]; then
		cat ${PROFILE_ARC_PROC} >${PROC_DIR}/arcstats.txt
	fi

	if [ -f ${PROFILE_VDEV_CACHE_PROC} ]; then
		cat ${PROFILE_VDEV_CACHE_PROC} >${PROC_DIR}/vdev_cache_stats.txt
	fi
}

zpios_profile_post_oprofile_stop() {
	local OPROFILE_LOG=$1
	local OPROFILE_ARGS="-a -g -l -p ${OPROFILE_KERNEL_DIR},${OPROFILE_SPL_DIR},${OPROFILE_ZFS_DIR}"

	/usr/bin/opcontrol --stop >>${OPROFILE_LOG} 2>&1
	/usr/bin/opcontrol --dump >>${OPROFILE_LOG} 2>&1
	/usr/bin/opreport ${OPROFILE_ARGS} >${OPROFILE_LOG} 2>&1
	/usr/bin/oparchive
}

zpios_profile_post_create() {
	zpios_profile_post_oprofile_stop ${PROFILE_RUN_CR_OPROFILE_LOG}
	zpios_profile_post_proc_stop ${PROFILE_RUN_CR_DIR}
	zpios_profile_post_stop ${PROFILE_RUN_CR_PID}
}

zpios_profile_post_write() {
	zpios_profile_post_oprofile_stop ${PROFILE_RUN_WR_OPROFILE_LOG}
	zpios_profile_post_proc_stop ${PROFILE_RUN_WR_DIR}
	zpios_profile_post_stop ${PROFILE_RUN_WR_PID}
}

zpios_profile_post_read() {
	zpios_profile_post_oprofile_stop ${PROFILE_RUN_CR_RD_LOG}
	zpios_profile_post_proc_stop ${PROFILE_RUN_RD_DIR}
	zpios_profile_post_stop ${PROFILE_RUN_RD_PID}
}

zpios_profile_post_remove() {
	zpios_profile_post_oprofile_stop ${PROFILE_RUN_RM_OPROFILE_LOG}
	zpios_profile_post_proc_stop ${PROFILE_RUN_RM_DIR}
	zpios_profile_post_stop ${PROFILE_RUN_RM_PID}
}

# Source global zpios test configuration
if [ -f ${RUN_DIR}/zpios-config.sh ]; then
	. ${RUN_DIR}/zpios-config.sh
fi

# Source global per-run test configuration
if [ -f ${RUN_DIR}/${RUN_ID}/zpios-config-run.sh ]; then
	. ${RUN_DIR}/${RUN_ID}/zpios-config-run.sh
fi

case "${RUN_PHASE}" in
	post-run)
		zpios_profile_post_pids
		zpios_profile_post_disk
		;;
	post-create)
		zpios_profile_post_create
		;;
	post-write)
		zpios_profile_post_write
		;;
	post-read)
		zpios_profile_post_read
		;;
	post-remove)
		zpios_profile_post_remove
		;;
	*)
		echo "Usage: ${PROG} {post-run|post-create|post-write|post-read|post-remove}"
		exit 1
esac

exit 0
