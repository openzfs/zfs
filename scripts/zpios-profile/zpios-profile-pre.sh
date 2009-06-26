#!/bin/bash

PROG=zpios-profile-pre.sh

PROFILE_RDY=0
trap "PROFILE_RDY=1" SIGHUP

RUN_PRE=${0}
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

zpios_profile_pre_run_cfg() {
cat > ${RUN_DIR}/${RUN_ID}/zpios-config-run.sh << EOF
#
# Zpios Profiling Configuration for Run ${RUN_ID}
#

PROFILE_RUN_DIR=${RUN_DIR}/${RUN_ID}

PROFILE_RUN_CR_DIR=${RUN_DIR}/${RUN_ID}/create
PROFILE_RUN_CR_PID=${RUN_DIR}/${RUN_ID}/create/profile.pid
PROFILE_RUN_CR_OPROFILE_LOG=${RUN_DIR}/${RUN_ID}/create/oprofile.txt
PROFILE_RUN_CR_PIDS_LOG=${RUN_DIR}/${RUN_ID}/create/pids.txt
PROFILE_RUN_CR_PIDS_CSV=${RUN_DIR}/${RUN_ID}/create/pids.csv
PROFILE_RUN_CR_DISK_LOG=${RUN_DIR}/${RUN_ID}/create/disk.txt
PROFILE_RUN_CR_DISK_CSV=${RUN_DIR}/${RUN_ID}/create/disk.csv

PROFILE_RUN_WR_DIR=${RUN_DIR}/${RUN_ID}/write
PROFILE_RUN_WR_PID=${RUN_DIR}/${RUN_ID}/write/profile.pid
PROFILE_RUN_WR_OPROFILE_LOG=${RUN_DIR}/${RUN_ID}/write/oprofile.txt
PROFILE_RUN_WR_PIDS_LOG=${RUN_DIR}/${RUN_ID}/write/pids.txt
PROFILE_RUN_WR_PIDS_CSV=${RUN_DIR}/${RUN_ID}/write/pids.csv
PROFILE_RUN_WR_DISK_LOG=${RUN_DIR}/${RUN_ID}/write/disk.txt
PROFILE_RUN_WR_DISK_CSV=${RUN_DIR}/${RUN_ID}/write/disk.csv

PROFILE_RUN_RD_DIR=${RUN_DIR}/${RUN_ID}/read
PROFILE_RUN_RD_PID=${RUN_DIR}/${RUN_ID}/read/profile.pid
PROFILE_RUN_RD_OPROFILE_LOG=${RUN_DIR}/${RUN_ID}/read/oprofile.txt
PROFILE_RUN_RD_PIDS_LOG=${RUN_DIR}/${RUN_ID}/read/pids.txt
PROFILE_RUN_RD_PIDS_CSV=${RUN_DIR}/${RUN_ID}/read/pids.csv
PROFILE_RUN_RD_DISK_LOG=${RUN_DIR}/${RUN_ID}/read/disk.txt
PROFILE_RUN_RD_DISK_CSV=${RUN_DIR}/${RUN_ID}/read/disk.csv

PROFILE_RUN_RM_DIR=${RUN_DIR}/${RUN_ID}/remove
PROFILE_RUN_RM_PID=${RUN_DIR}/${RUN_ID}/remove/profile.pid
PROFILE_RUN_RM_OPROFILE_LOG=${RUN_DIR}/${RUN_ID}/remove/oprofile.txt
PROFILE_RUN_RM_PIDS_LOG=${RUN_DIR}/${RUN_ID}/remove/pids.txt
PROFILE_RUN_RM_PIDS_CSV=${RUN_DIR}/${RUN_ID}/remove/pids.csv
PROFILE_RUN_RM_DISK_LOG=${RUN_DIR}/${RUN_ID}/remove/disk.txt
PROFILE_RUN_RM_DISK_CSV=${RUN_DIR}/${RUN_ID}/remove/disk.csv

# PROFILE_PIDS_LOG=${RUN_DIR}/${RUN_ID}/pids-summary.csv
# PROFILE_DISK_LOG=${RUN_DIR}/${RUN_ID}/disk-summary.csv
EOF
}

zpios_profile_pre_run_args() {
cat > ${RUN_DIR}/${RUN_ID}/zpios-args.txt << EOF
#
# Zpios Arguments for Run ${RUN_ID}
#

DIR=${RUN_DIR}
ID=${RUN_ID}
POOL=${RUN_POOL}
CHUNK_SIZE=${RUN_CHUNK_SIZE}
REGION_SIZE=${RUN_REGION_SIZE}
THRD_COUNT=${RUN_THRD_COUNT}
REGION_COUNT=${RUN_REGION_COUNT}
OFFSET=${RUN_OFFSET}
REGION_NOISE=${RUN_REGION_NOISE}
CHUNK_NOISE=${RUN_CHUNK_NOISE}
THRD_DELAY=${RUN_THRD_DELAY}
FLAGS=${RUN_FLAGS}
RESULT=${RUN_RESULT}
EOF
}

# Spawn a user defined profiling script to gather additional data
zpios_profile_pre_start() {
	local PROFILE_PID=$1

	${PROFILE_USER} ${RUN_PHASE} ${RUN_DIR} ${RUN_ID} &
	echo "$!" >${PROFILE_PID}

	# Sleep waiting for profile script to be ready, it will
	# signal us via SIGHUP when it is ready to start profiling.
	while [ ${PROFILE_RDY} -eq 0 ]; do
		sleep 0.01
	done
}

zpios_profile_post_proc_start() { 

        if [ -f ${PROFILE_ARC_PROC} ]; then
                echo 0 >${PROFILE_ARC_PROC}
        fi

        if [ -f ${PROFILE_VDEV_CACHE_PROC} ]; then
                echo 0 >${PROFILE_VDEV_CACHE_PROC}
        fi
}

zpios_profile_pre_oprofile_start() {
	local OPROFILE_LOG=$1

	/usr/bin/opcontrol --reset >>${OPROFILE_LOG} 2>&1
	/usr/bin/opcontrol --start >>${OPROFILE_LOG} 2>&1
}

zpios_profile_pre_create() {
	mkdir ${PROFILE_RUN_CR_DIR}
	zpios_profile_pre_start ${PROFILE_RUN_CR_PID}
	zpios_profile_post_proc_start
	zpios_profile_pre_oprofile_start ${PROFILE_RUN_CR_OPROFILE_LOG}
}

zpios_profile_pre_write() {
	mkdir ${PROFILE_RUN_WR_DIR}
	zpios_profile_pre_start ${PROFILE_RUN_WR_PID}
	zpios_profile_post_proc_start
	zpios_profile_pre_oprofile_start ${PROFILE_RUN_WR_OPROFILE_LOG}
}

zpios_profile_pre_read() {
	mkdir ${PROFILE_RUN_RD_DIR}
	zpios_profile_pre_start ${PROFILE_RUN_RD_PID}
	zpios_profile_post_proc_start
	zpios_profile_pre_oprofile_start ${PROFILE_RUN_CR_RD_LOG}
}

zpios_profile_pre_remove() {
	mkdir ${PROFILE_RUN_RM_DIR}
	zpios_profile_pre_start ${PROFILE_RUN_RM_PID}
	zpios_profile_post_proc_start
	zpios_profile_pre_oprofile_start ${PROFILE_RUN_RM_OPROFILE_LOG}
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
	pre-run)
		mkdir -p ${RUN_DIR}/${RUN_ID}/
		zpios_profile_pre_run_cfg
		zpios_profile_pre_run_args
		;;
	pre-create)
		zpios_profile_pre_create
		;;
	pre-write)
		zpios_profile_pre_write
		;;
	pre-read)
		zpios_profile_pre_read
		;;
	pre-remove)
		zpios_profile_pre_remove
		;;
	*)
		echo "Usage: ${PROG} {pre-run|pre-create|pre-write|pre-read|pre-remove}"
		exit 1
esac

exit 0
