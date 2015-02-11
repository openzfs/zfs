#!/bin/bash
#
# 1 scsi_debug devices on top of which is layered no raid.
#

SDSIZE=${SDSIZE:-128}
SDHOSTS=${SDHOSTS:-1}
SDTGTS=${SDTGTS:-1}
SDLUNS=${SDLUNS:-1}
LDMOD=/sbin/modprobe

zpool_create() {
	check_sd_utils

	test `${LSMOD} | grep -c scsi_debug` -gt 0 && \
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

	msg "${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${SDDEVICE}"
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${SDDEVICE} ||           \
		(${RMMOD} scsi_debug && exit 1)
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	msg "${RMMOD} scsi_debug"
	${RMMOD} scsi_debug || die "Error $? removing scsi_debug devices"
}
