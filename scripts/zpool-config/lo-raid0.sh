#!/bin/bash
#
# 5 Device Loopback Raid-0 Configuration
#

DEVICES="/tmp/zpool-vdev0  \
         /tmp/zpool-vdev1  \
         /tmp/zpool-vdev2  \
         /tmp/zpool-vdev3  \
         /tmp/zpool-vdev4" 
DEVICE_SIZE=512

zpool_create() {
	for DEV in ${DEVICES}; do
		LO=`/sbin/losetup -f`
		msg "Creating ${DEV} using loopback device ${LO}"
		rm -f ${DEV} || exit 1
		dd if=/dev/zero of=${DEV} bs=1024k count=${DEVICE_SIZE} &>/dev/null || exit 1
		losetup ${LO} ${DEV} || exit 1
	done

	msg "${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES}"
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES} || exit 1
}

zpool_destroy() {
	msg "${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}"
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}

	for DEV in ${DEVICES}; do
		LO=`/sbin/losetup -a | grep ${DEV} | head -n1 | cut -f1 -d:`
		msg "Removing ${DEV} using loopback device ${LO}"
		rm -f ${DEV} || exit 1
	done
}
