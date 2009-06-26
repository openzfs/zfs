#!/bin/bash
#
# /proc/diskinfo <after skipping major/minor>
# Field 1 -- device name
# Field 2 -- # of reads issued
# Field 3 -- # of reads merged
# Field 4 -- # of sectors read
# Field 5 -- # of milliseconds spent reading
# Field 6 -- # of writes completed
# Field 7 -- # of writes merged
# Field 8 -- # of sectors written
# Field 9 -- # of milliseconds spent writing
# Field 10 -- # of I/Os currently in progress
# Field 11 -- # of milliseconds spent doing I/Os
# Field 12 -- weighted # of milliseconds spent doing I/Os

PROG=zpios-profile-disk.sh

RUN_PIDS=${0}
RUN_LOG_DIR=${1}
RUN_ID=${2}

create_table() {
	local FIELD=$1
	local ROW_M=()
	local ROW_N=()
	local HEADER=1
	local STEP=1

	for DISK_FILE in `ls -r --sort=time --time=ctime ${RUN_LOG_DIR}/${RUN_ID}/disk-[0-9]*`; do
		ROW_M=( ${ROW_N[@]} )
		ROW_N=( `cat ${DISK_FILE} | grep sd | cut -c11- | cut -f${FIELD} -d' ' | tr "\n" "\t"` )

		if [ $HEADER -eq 1 ]; then
			echo -n "step, "
			cat ${DISK_FILE} | grep sd | cut -c11- | cut -f1 -d' ' | tr "\n" ", "
	                echo "total"
			HEADER=0
		fi

		if [ ${#ROW_M[@]} -eq 0 ]; then
			continue
		fi

		if [ ${#ROW_M[@]} -ne ${#ROW_N[@]} ]; then
			echo "Badly formatted profile data in ${DISK_FILE}"
			break
		fi

		TOTAL=0
		echo -n "${STEP}, "
		for (( i=0; i<${#ROW_N[@]}; i++ )); do
			DELTA=`echo "${ROW_N[${i}]}-${ROW_M[${i}]}" | bc`
			let TOTAL=${TOTAL}+${DELTA}
			echo -n "${DELTA}, "
		done
		echo "${TOTAL}, "

		let STEP=${STEP}+1
	done
}

create_table_mbs() {
	local FIELD=$1
	local TIME=$2
	local ROW_M=()
	local ROW_N=()
	local HEADER=1
	local STEP=1

	for DISK_FILE in `ls -r --sort=time --time=ctime ${RUN_LOG_DIR}/${RUN_ID}/disk-[0-9]*`; do
		ROW_M=( ${ROW_N[@]} )
		ROW_N=( `cat ${DISK_FILE} | grep sd | cut -c11- | cut -f${FIELD} -d' ' | tr "\n" "\t"` )

		if [ $HEADER -eq 1 ]; then
			echo -n "step, "
			cat ${DISK_FILE} | grep sd | cut -c11- | cut -f1 -d' ' | tr "\n" ", "
	                echo "total"
			HEADER=0
		fi

		if [ ${#ROW_M[@]} -eq 0 ]; then
			continue
		fi

		if [ ${#ROW_M[@]} -ne ${#ROW_N[@]} ]; then
			echo "Badly formatted profile data in ${DISK_FILE}"
			break
		fi

		TOTAL=0
		echo -n "${STEP}, "
		for (( i=0; i<${#ROW_N[@]}; i++ )); do
			DELTA=`echo "${ROW_N[${i}]}-${ROW_M[${i}]}" | bc`
			MBS=`echo "scale=2; ((${DELTA}*512)/${TIME})/(1024*1024)" | bc`
			TOTAL=`echo "scale=2; ${TOTAL}+${MBS}" | bc`
			echo -n "${MBS}, "
		done
		echo "${TOTAL}, "

		let STEP=${STEP}+1
	done
}

echo
echo "Reads issued per device"
create_table 2
echo
echo "Reads merged per device"
create_table 3
echo
echo "Sectors read per device"
create_table 4
echo "MB/s per device"
create_table_mbs 4 3

echo
echo "Writes issued per device"
create_table 6
echo
echo "Writes merged per device"
create_table 7
echo
echo "Sectors written per device"
create_table 8
echo "MB/s per device"
create_table_mbs 8 3

exit 0
