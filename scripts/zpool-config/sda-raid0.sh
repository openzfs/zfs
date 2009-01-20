#!/bin/bash
#
# Single disk /dev/sda Raid-0 Configuration
#

DEVICES="/dev/sda"

zpool_create() {
	msg "${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES}"
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES} || exit 1
}

zpool_destroy() {
	msg "${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}"
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME} || exit 1
}
