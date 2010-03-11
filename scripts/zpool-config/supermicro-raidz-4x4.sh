#!/bin/bash
#
# Supermicro (White Box) Raid-Z Configuration (4x4(3+1))
#

RANKS=4
CHANNELS=4

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.supermicro.example
	udev_raidz_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.supermicro.example
}
