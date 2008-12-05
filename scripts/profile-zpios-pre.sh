#!/bin/bash
# profile-zpios-pre.sh

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

PROFILE_ZPIOS_BIN=/home/behlendo/src/zfs/scripts/profile-zpios.sh
PROFILE_ZPIOS_READY=0

OPROFILE_LOG=${RUN_LOG_DIR}/${RUN_ID}/oprofile.txt
PROFILE_PID=${RUN_LOG_DIR}/${RUN_ID}/pid
RUN_ARGS=${RUN_LOG_DIR}/${RUN_ID}/args

if [ "${RUN_PHASE}" != "pre" ]; then
        exit 1
fi

rm -Rf ${RUN_LOG_DIR}/${RUN_ID}/
mkdir -p ${RUN_LOG_DIR}/${RUN_ID}/

echo "PHASE=${RUN_PHASE}" >>${RUN_ARGS}
echo "LOG_DIR=${RUN_LOG_DIR}" >>${RUN_ARGS}
echo "ID=${RUN_ID}" >>${RUN_ARGS}
echo "POOL=${RUN_POOL}" >>${RUN_ARGS}
echo "CHUNK_SIZE=${RUN_CHUNK_SIZE}" >>${RUN_ARGS}
echo "REGION_SIZE=${RUN_REGION_SIZE}" >>${RUN_ARGS}
echo "THREAD_COUNT=${RUN_THREAD_COUNT}" >>${RUN_ARGS}
echo "REGION_COUNT=${RUN_REGION_COUNT}" >>${RUN_ARGS}
echo "OFFSET=${RUN_OFFSET}" >>${RUN_ARGS}
echo "REGION_NOISE=${RUN_REGION_NOISE}" >>${RUN_ARGS}
echo "CHUNK_NOISE=${RUN_CHUNK_NOISE}" >>${RUN_ARGS}
echo "THREAD_DELAY=${RUN_THREAD_DELAY}" >>${RUN_ARGS}
echo "FLAGS=${RUN_FLAGS}" >>${RUN_ARGS}
echo "RESULT=${RUN_RESULT}" >>${RUN_ARGS}

# XXX: Oprofile support seems to be broken when I try and start
# it via a user mode helper script, I suspect the setup is failing.
# opcontrol --init >>${OPROFILE_LOG} 2>&1
# opcontrol --setup --vmlinux=/boot/vmlinux >>${OPROFILE_LOG} 2>&1

# Start the profile script
${PROFILE_ZPIOS_BIN} ${RUN_PHASE} ${RUN_LOG_DIR} ${RUN_ID} &
echo "$!" >${PROFILE_PID}

# Sleep waiting for profile script to be ready, it will
# signal us via SIGHUP when it is ready to start profiling.
while [ ${PROFILE_ZPIOS_READY} -eq 0 ]; do
	sleep 0.1
done

# opcontrol --start-daemon >>${OPROFILE_LOG} 2>&1
# opcontrol --start >>${OPROFILE_LOG} 2>&1

exit 0
