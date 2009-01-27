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
	for FILE in ${FILES}; do
		DEVICE=`/sbin/losetup -f`
		msg "Creating ${FILE} using loopback device ${DEVICE}"
		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=256 &>/dev/null ||
			die "Error $? creating ${FILE}"
		losetup ${DEVICE} ${FILE} ||
			die "Error $? creating ${FILE} -> ${DEVICE} loopback"
		DEVICES="${DEVICES} ${DEVICE}"
	done

	msg ${CMDDIR}/zpool/zpool create ${ZPOOL_NAME} raidz ${DEVICES}
	${CMDDIR}/zpool/zpool create ${ZPOOL_NAME} raidz ${DEVICES} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		DEVICE=`/sbin/losetup -a | grep ${FILE} | head -n1|cut -f1 -d:`
		msg "Removing ${FILE} using loopback device ${DEVICE}"
		losetup -d ${DEVICE} ||
			die "Error $? destroying ${FILE} -> ${DEVICE} loopback"
		rm -f ${FILE} || exit 1
	done
}
