#!/bin/bash
#
# 1 scsi_debug device for fault injection and 3 loopback devices
# on top of which is layered raid10 (mirrored).
#

SDSIZE=${SDSIZE:-256}
SDHOSTS=${SDHOSTS:-1}
SDTGTS=${SDTGTS:-1}
SDLUNS=${SDLUNS:-1}
LDMOD=/sbin/modprobe
FILEDIR=${FILEDIR:-/var/tmp}
FILES=${FILES:-"$FILEDIR/file-vdev0 $FILEDIR/file-vdev1 $FILEDIR/file-vdev2"}
DEVICES_M1=""
DEVICES_M2=""

zpool_create() {
	local COUNT=0

	check_loop_utils
	check_sd_utils

	test `${LSMOD} | grep -c scsi_debug` -gt 0 &&                        \
		(echo 0 >/sys/module/scsi_debug/parameters/every_nth &&      \
		${RMMOD} scsi_debug || exit 1)
	udev_trigger

	msg "${LDMOD} scsi_debug dev_size_mb=${SDSIZE} "                     \
		"add_host=${SDHOSTS} num_tgts=${SDTGTS} "                    \
		"max_luns=${SDLUNS}"
	${LDMOD} scsi_debug                                                  \
		dev_size_mb=${SDSIZE}                                        \
		add_host=${SDHOSTS}                                          \
		num_tgts=${SDTGTS}                                           \
		max_luns=${SDLUNS} ||                                        \
		die "Error $? creating scsi_debug devices"
	udev_trigger
	SDDEVICE=`${LSSCSI}|${AWK} '/scsi_debug/ { print $6; exit }'`
	msg "${PARTED} -s ${SDDEVICE} mklabel gpt"
	${PARTED} -s ${SDDEVICE} mklabel gpt ||                              \
		(${RMMOD} scsi_debug && die "Error $? creating gpt label")

	for FILE in ${FILES}; do
		LODEVICE=`unused_loop_device`

		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=0 seek=256         \
			&>/dev/null || (${RMMOD} scsi_debug &&               \
			die "Error $? creating ${FILE}")

		# Setup the loopback device on the file.
		msg "Creating ${LODEVICE} using ${FILE}"
		${LOSETUP} ${LODEVICE} ${FILE} || (${RMMOD} scsi_debug       \
			die "Error $? creating ${LODEVICE} using ${FILE}")

		DEVICES="${DEVICES} ${LODEVICE}"
	done

        DEVICES="${DEVICES} ${SDDEVICE}"

        for DEVICE in ${DEVICES}; do
		let COUNT=${COUNT}+1

		if [ $((COUNT % 2)) -eq 0 ]; then
			DEVICES_M2="${DEVICES_M2} ${DEVICE}"
		else
			DEVICES_M1="${DEVICES_M1} ${DEVICE}"
		fi
	done

	msg "${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} "                   \
		"mirror ${DEVICES_M1} mirror ${DEVICES_M2}"
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME}                          \
		mirror ${DEVICES_M1} mirror ${DEVICES_M2} ||                 \
		(${RMMOD} scsi_debug && exit 1)
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		LODEVICE=`${LOSETUP} -a | grep ${FILE} | head -n1|cut -f1 -d:`
		msg "Removing ${LODEVICE} using ${FILE}"
		${LOSETUP} -d ${LODEVICE} ||
			die "Error $? destroying ${LODEVICE} using ${FILE}"
		rm -f ${FILE} || exit 1
	done

	msg "${RMMOD} scsi_debug"
	${RMMOD} scsi_debug || die "Error $? removing scsi_debug devices"
}
