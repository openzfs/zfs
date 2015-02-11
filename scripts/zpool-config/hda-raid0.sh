#!/bin/bash
#
# Single disk /dev/hda Raid-0 Configuration
#

DEVICES="/dev/hda"

zpool_create() {
	msg ${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${DEVICES}
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${DEVICES} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME} || exit 1
}
