#!/bin/bash
#
# Sun Fire x4550 (Thumper/Thor) Raid-Z Configuration (8x6(5+1))
#

RANKS=8
CHANNELS=6

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.x4550.example
	udev_raidz_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.x4550.example
}
