#!/bin/bash
#
# 4 Device Loopback Raid-0 Configuration
#

FILES="/tmp/zpool-vdev0  \
       /tmp/zpool-vdev1  \
       /tmp/zpool-vdev2  \
       /tmp/zpool-vdev3"
DEVICES=""

zpool_create() {
	check_loop_utils

	for FILE in ${FILES}; do
		DEVICE=`unused_loop_device`
		msg "Creating ${FILE} using loopback device ${DEVICE}"
		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=0 seek=256 \
			&>/dev/null || die "Error $? creating ${FILE}"
		${LOSETUP} ${DEVICE} ${FILE} ||
			die "Error $? creating ${FILE} -> ${DEVICE} loopback"
		DEVICES="${DEVICES} ${DEVICE}"
	done

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} raidz2 ${DEVICES}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} raidz2 ${DEVICES} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		DEVICE=`${LOSETUP} -a | grep ${FILE} | head -n1|cut -f1 -d:`
		msg "Removing ${FILE} using loopback device ${DEVICE}"
		${LOSETUP} -d ${DEVICE} ||
			die "Error $? destroying ${FILE} -> ${DEVICE} loopback"
		rm -f ${FILE} || exit 1
	done
}
