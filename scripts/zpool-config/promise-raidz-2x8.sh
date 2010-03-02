#!/bin/bash
#
# Flash (White Box) Raid-Z Configuration (2x8(7+1))
#

RANKS=8
CHANNELS=2

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.promise.example
	udev_raidz_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
