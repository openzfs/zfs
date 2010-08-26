#!/bin/bash


PROG=zpios-profile.sh

trap "RUN_DONE=1" SIGHUP

RUN_PHASE=${1}
RUN_LOG_DIR=${2}
RUN_ID=${3}
RUN_DONE=0

POLL_INTERVAL=2.99

# Log these pids, the exact pid numbers will vary from system to system
# so I harvest pid for all the following type of processes from /proc/<pid>/
#
# zio_taskq/#
# spa_zio_issue/#
# spa_zio_intr/#
# txg_quiesce_thr
# txg_sync_thread
# txg_timelimit_t
# arc_reclaim_thr
# l2arc_feed_thre
# zpios_io/#

ZIO_TASKQ_PIDS=()
ZIO_REQ_NUL_PIDS=() 
ZIO_IRQ_NUL_PIDS=() 
ZIO_REQ_RD_PIDS=() 
ZIO_IRQ_RD_PIDS=() 
ZIO_REQ_WR_PIDS=() 
ZIO_IRQ_WR_PIDS=()
ZIO_REQ_FR_PIDS=() 
ZIO_IRQ_FR_PIDS=()
ZIO_REQ_CM_PIDS=() 
ZIO_IRQ_CM_PIDS=()
ZIO_REQ_CTL_PIDS=() 
ZIO_IRQ_CTL_PIDS=()

TXG_QUIESCE_PIDS=()
TXG_SYNC_PIDS=() 
TXG_TIMELIMIT_PIDS=()

ARC_RECLAIM_PIDS=()
L2ARC_FEED_PIDS=()

ZPIOS_IO_PIDS=()

show_pids() {
	echo "* zio_taskq:     { ${ZIO_TASKQ_PIDS[@]} } = ${#ZIO_TASKQ_PIDS[@]}"
	echo "* zio_req_nul:   { ${ZIO_REQ_NUL_PIDS[@]} } = ${#ZIO_REQ_NUL_PIDS[@]}"
	echo "* zio_irq_nul:   { ${ZIO_IRQ_NUL_PIDS[@]} } = ${#ZIO_IRQ_NUL_PIDS[@]}"
	echo "* zio_req_rd:    { ${ZIO_REQ_RD_PIDS[@]} } = ${#ZIO_REQ_RD_PIDS[@]}"
	echo "* zio_irq_rd:    { ${ZIO_IRQ_RD_PIDS[@]} } = ${#ZIO_IRQ_RD_PIDS[@]}"
	echo "* zio_req_wr:    { ${ZIO_REQ_WR_PIDS[@]} } = ${#ZIO_REQ_WR_PIDS[@]}"
	echo "* zio_irq_wr:    { ${ZIO_IRQ_WR_PIDS[@]} } = ${#ZIO_IRQ_WR_PIDS[@]}"
	echo "* zio_req_fr:    { ${ZIO_REQ_FR_PIDS[@]} } = ${#ZIO_REQ_FR_PIDS[@]}"
	echo "* zio_irq_fr:    { ${ZIO_IRQ_FR_PIDS[@]} } = ${#ZIO_IRQ_FR_PIDS[@]}"
	echo "* zio_req_cm:    { ${ZIO_REQ_CM_PIDS[@]} } = ${#ZIO_REQ_CM_PIDS[@]}"
	echo "* zio_irq_cm:    { ${ZIO_IRQ_CM_PIDS[@]} } = ${#ZIO_IRQ_CM_PIDS[@]}"
	echo "* zio_req_ctl:   { ${ZIO_REQ_CTL_PIDS[@]} } = ${#ZIO_REQ_CTL_PIDS[@]}"
	echo "* zio_irq_ctl:   { ${ZIO_IRQ_CTL_PIDS[@]} } = ${#ZIO_IRQ_CTL_PIDS[@]}"
	echo "* txg_quiesce:   { ${TXG_QUIESCE_PIDS[@]} } = ${#TXG_QUIESCE_PIDS[@]}"
	echo "* txg_sync:      { ${TXG_SYNC_PIDS[@]} } = ${#TXG_SYNC_PIDS[@]}"
	echo "* txg_timelimit: { ${TXG_TIMELIMIT_PIDS[@]} } = ${#TXG_TIMELIMIT_PIDS[@]}"
	echo "* arc_reclaim:   { ${ARC_RECLAIM_PIDS[@]} } = ${#ARC_RECLAIM_PIDS[@]}"
	echo "* l2arc_feed:    { ${L2ARC_FEED_PIDS[@]} } = ${#L2ARC_FEED_PIDS[@]}"
	echo "* zpios_io:      { ${ZPIOS_IO_PIDS[@]} } = ${#ZPIOS_IO_PIDS[@]}"
}

check_pid() {
	local PID=$1
	local NAME=$2
	local TYPE=$3
	local PIDS=( "$4" )
        local NAME_STRING=`echo ${NAME} | cut -f1 -d'/'`
        local NAME_NUMBER=`echo ${NAME} | cut -f2 -d'/'`

	if [ "${NAME_STRING}" == "${TYPE}" ]; then
		if [ -n "${NAME_NUMBER}" ]; then
			PIDS[${NAME_NUMBER}]=${PID}
		else
			PIDS[${#PIDS[@]}]=${PID}

		fi
	fi

	echo "${PIDS[@]}"
}

# NOTE: This whole process is crazy slow but it will do for now
aquire_pids() {
	echo "--- Aquiring ZFS pids ---"

	for PID in `ls /proc/ | grep [0-9] | sort -n -u`; do
		if [ ! -e /proc/${PID}/status ]; then
			continue
		fi

		NAME=`cat /proc/${PID}/status  | head -n1 | cut -f2`

		ZIO_TASKQ_PIDS=( `check_pid ${PID} ${NAME} "zio_taskq" \
		                 "$(echo "${ZIO_TASKQ_PIDS[@]}")"` )

		ZIO_REQ_NUL_PIDS=( `check_pid ${PID} ${NAME} "zio_req_nul" \
		                   "$(echo "${ZIO_REQ_NUL_PIDS[@]}")"` )

		ZIO_IRQ_NUL_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_nul" \
		                   "$(echo "${ZIO_IRQ_NUL_PIDS[@]}")"` )

		ZIO_REQ_RD_PIDS=( `check_pid ${PID} ${NAME} "zio_req_rd" \
		                   "$(echo "${ZIO_REQ_RD_PIDS[@]}")"` )

		ZIO_IRQ_RD_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_rd" \
		                   "$(echo "${ZIO_IRQ_RD_PIDS[@]}")"` )

		ZIO_REQ_WR_PIDS=( `check_pid ${PID} ${NAME} "zio_req_wr" \
		                   "$(echo "${ZIO_REQ_WR_PIDS[@]}")"` )

		ZIO_IRQ_WR_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_wr" \
		                   "$(echo "${ZIO_IRQ_WR_PIDS[@]}")"` )

		ZIO_REQ_FR_PIDS=( `check_pid ${PID} ${NAME} "zio_req_fr" \
		                   "$(echo "${ZIO_REQ_FR_PIDS[@]}")"` )

		ZIO_IRQ_FR_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_fr" \
		                   "$(echo "${ZIO_IRQ_FR_PIDS[@]}")"` )

		ZIO_REQ_CM_PIDS=( `check_pid ${PID} ${NAME} "zio_req_cm" \
		                   "$(echo "${ZIO_REQ_CM_PIDS[@]}")"` )

		ZIO_IRQ_CM_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_cm" \
		                   "$(echo "${ZIO_IRQ_CM_PIDS[@]}")"` )

		ZIO_REQ_CTL_PIDS=( `check_pid ${PID} ${NAME} "zio_req_ctl" \
		                   "$(echo "${ZIO_REQ_CTL_PIDS[@]}")"` )

		ZIO_IRQ_CTL_PIDS=( `check_pid ${PID} ${NAME} "zio_irq_ctl" \
		                   "$(echo "${ZIO_IRQ_CTL_PIDS[@]}")"` )

		TXG_QUIESCE_PIDS=( `check_pid ${PID} ${NAME} "txg_quiesce" \
		                   "$(echo "${TXG_QUIESCE_PIDS[@]}")"` )

		TXG_SYNC_PIDS=( `check_pid ${PID} ${NAME} "txg_sync" \
		                "$(echo "${TXG_SYNC_PIDS[@]}")"` )

		TXG_TIMELIMIT_PIDS=( `check_pid ${PID} ${NAME} "txg_timelimit" \
		                     "$(echo "${TXG_TIMELIMIT_PIDS[@]}")"` )

		ARC_RECLAIM_PIDS=( `check_pid ${PID} ${NAME} "arc_reclaim" \
                                     "$(echo "${ARC_RECLAIM_PIDS[@]}")"` )

		L2ARC_FEED_PIDS=( `check_pid ${PID} ${NAME} "l2arc_feed" \
                                  "$(echo "${L2ARC_FEED_PIDS[@]}")"` )
	done

	# Wait for zpios_io threads to start
	kill -s SIGHUP ${PPID}
	echo "* Waiting for zpios_io threads to start"
	while [ ${RUN_DONE} -eq 0 ]; do
		ZPIOS_IO_PIDS=( `ps ax | grep zpios_io | grep -v grep | \
                                 sed 's/^ *//g' | cut -f1 -d' '` )
		if [ ${#ZPIOS_IO_PIDS[@]} -gt 0 ]; then
			break;
		fi
		sleep 0.1
	done

	echo "`show_pids`" >${RUN_LOG_DIR}/${RUN_ID}/pids.txt
}

log_pids() {
	echo "--- Logging ZFS profile to ${RUN_LOG_DIR}/${RUN_ID}/ ---"
	ALL_PIDS=( ${ZIO_TASKQ_PIDS[@]}     \
                   ${ZIO_REQ_NUL_PIDS[@]}   \
                   ${ZIO_IRQ_NUL_PIDS[@]}   \
                   ${ZIO_REQ_RD_PID[@]}     \
                   ${ZIO_IRQ_RD_PIDS[@]}    \
                   ${ZIO_REQ_WR_PIDS[@]}    \
                   ${ZIO_IRQ_WR_PIDS[@]}    \
                   ${ZIO_REQ_FR_PIDS[@]}    \ 
                   ${ZIO_IRQ_FR_PIDS[@]}    \
                   ${ZIO_REQ_CM_PIDS[@]}    \ 
                   ${ZIO_IRQ_CM_PIDS[@]}    \
                   ${ZIO_REQ_CTL_PIDS[@]}   \
                   ${ZIO_IRQ_CTL_PIDS[@]}   \
                   ${TXG_QUIESCE_PIDS[@]}   \
                   ${TXG_SYNC_PIDS[@]}      \
                   ${TXG_TIMELIMIT_PIDS[@]} \
                   ${ARC_RECLAIM_PIDS[@]}   \
                   ${L2ARC_FEED_PIDS[@]}    \
                   ${ZPIOS_IO_PIDS[@]} )

	while [ ${RUN_DONE} -eq 0 ]; do
		NOW=`date +%s.%N`
		LOG_PIDS="${RUN_LOG_DIR}/${RUN_ID}/pids-${NOW}"
		LOG_DISK="${RUN_LOG_DIR}/${RUN_ID}/disk-${NOW}"

		for PID in "${ALL_PIDS[@]}"; do
			if [ -z ${PID} ]; then
				continue;
			fi

	        	if [ -e /proc/${PID}/stat ]; then
	        		cat /proc/${PID}/stat | head -n1 >>${LOG_PIDS}
			else
	                	echo "<${PID} exited>" >>${LOG_PIDS}
	        	fi
		done

		cat /proc/diskstats >${LOG_DISK}

		NOW2=`date +%s.%N`
		DELTA=`echo "${POLL_INTERVAL}-(${NOW2}-${NOW})" | bc`
		sleep ${DELTA}
	done
}

aquire_pids
log_pids

# rm ${PROFILE_PID}

exit 0
