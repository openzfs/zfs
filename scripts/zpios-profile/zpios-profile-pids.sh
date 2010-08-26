#!/bin/bash

PROG=zpios-profile-pids.sh

RUN_PIDS=${0}
RUN_LOG_DIR=${1}
RUN_ID=${2}

ROW_M=()
ROW_N=()
ROW_N_SCHED=()
ROW_N_WAIT=()

HEADER=1
STEP=1

for PID_FILE in `ls -r --sort=time --time=ctime ${RUN_LOG_DIR}/${RUN_ID}/pids-[0-9]*`; do
	ROW_M=( ${ROW_N[@]} )
	ROW_N=( 0 0 0 0  0 0 0 0  0 0 0 0  0 0 0 0  0 0 0 )
	ROW_N_SCHED=( `cat ${PID_FILE} | cut -f15 -d' ' | tr "\n" "\t"` )
	ROW_N_WAIT=(  `cat ${PID_FILE} | cut -f17 -d' ' | tr "\n" "\t"` )
	ROW_N_NAMES=( `cat ${PID_FILE} | cut -f2  -d' ' | cut -f2 -d'(' | 
                       cut -f1 -d')'   | cut -f1  -d'/' | tr "\n" "\t"` )

	for (( i=0; i<${#ROW_N_SCHED[@]}; i++ )); do
		SUM=`echo "${ROW_N_WAIT[${i}]}+${ROW_N_SCHED[${i}]}" | bc`

		case ${ROW_N_NAMES[${i}]} in
			zio_taskq)	IDX=0;;
			zio_req_nul)	IDX=1;;
			zio_irq_nul)	IDX=2;;
			zio_req_rd)	IDX=3;;
			zio_irq_rd)	IDX=4;;
			zio_req_wr)	IDX=5;;
			zio_irq_wr)	IDX=6;;
			zio_req_fr)	IDX=7;;
			zio_irq_fr)	IDX=8;;
			zio_req_cm)	IDX=9;;
			zio_irq_cm)	IDX=10;;
			zio_req_ctl)	IDX=11;;
			zio_irq_ctl)	IDX=12;;
			txg_quiesce)	IDX=13;;
			txg_sync)	IDX=14;;
			txg_timelimit)	IDX=15;;
			arc_reclaim)	IDX=16;;
			l2arc_feed)	IDX=17;;
			zpios_io)	IDX=18;;
			*)		continue;;
		esac

		let ROW_N[${IDX}]=${ROW_N[${IDX}]}+${SUM}
	done

	if [ $HEADER -eq 1 ]; then
		echo "step, zio_taskq, zio_req_nul, zio_irq_nul, "        \
                     "zio_req_rd, zio_irq_rd, zio_req_wr, zio_irq_wr, "   \
                     "zio_req_fr, zio_irq_fr, zio_req_cm, zio_irq_cm, "   \
                     "zio_req_ctl, zio_irq_ctl, txg_quiesce, txg_sync, "  \
                     "txg_timelimit, arc_reclaim, l2arc_feed, zpios_io, " \
		     "idle"
		HEADER=0
	fi

	if [ ${#ROW_M[@]} -eq 0 ]; then
		continue
	fi

	if [ ${#ROW_M[@]} -ne ${#ROW_N[@]} ]; then
		echo "Badly formatted profile data in ${PID_FILE}"
		break
	fi

	# Original values are in jiffies and we expect HZ to be 1000
	# on most 2.6 systems thus we divide by 10 to get a percentage.
	IDLE=1000
        echo -n "${STEP}, "
	for (( i=0; i<${#ROW_N[@]}; i++ )); do
		DELTA=`echo "${ROW_N[${i}]}-${ROW_M[${i}]}" | bc`
		DELTA_PERCENT=`echo "scale=1; ${DELTA}/10" | bc`
		let IDLE=${IDLE}-${DELTA}
		echo -n "${DELTA_PERCENT}, "
	done
	ILDE_PERCENT=`echo "scale=1; ${IDLE}/10" | bc`
	echo "${ILDE_PERCENT}"

	let STEP=${STEP}+1
done

exit

echo
echo "Percent of total system time per pid"
for PID_FILE in `ls -r --sort=time --time=ctime ${RUN_LOG_DIR}/${RUN_ID}/pids-[0-9]*`; do
	ROW_M=( ${ROW_N[@]} )
	ROW_N_SCHED=( `cat ${PID_FILE} | cut -f15 -d' ' | tr "\n" "\t"` )
	ROW_N_WAIT=( `cat ${PID_FILE} | cut -f17 -d' ' | tr "\n" "\t"` )

	for (( i=0; i<${#ROW_N_SCHED[@]}; i++ )); do
		ROW_N[${i}]=`echo "${ROW_N_WAIT[${i}]}+${ROW_N_SCHED[${i}]}" | bc`
	done

	if [ $HEADER -eq 1 ]; then
		echo -n "step, "
		cat ${PID_FILE} | cut -f2 -d' ' | tr "\n" ", "
		echo
		HEADER=0
	fi

	if [ ${#ROW_M[@]} -eq 0 ]; then
		continue
	fi

	if [ ${#ROW_M[@]} -ne ${#ROW_N[@]} ]; then
		echo "Badly formatted profile data in ${PID_FILE}"
		break
	fi

	# Original values are in jiffies and we expect HZ to be 1000
	# on most 2.6 systems thus we divide by 10 to get a percentage.
        echo -n "${STEP}, "
	for (( i=0; i<${#ROW_N[@]}; i++ )); do
		DELTA=`echo "scale=1; (${ROW_N[${i}]}-${ROW_M[${i}]})/10" | bc`
		echo -n "${DELTA}, "
	done

	echo
	let STEP=${STEP}+1
done


exit 0
