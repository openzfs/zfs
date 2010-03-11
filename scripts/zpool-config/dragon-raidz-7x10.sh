#!/bin/bash
#
# Dragon (White Box) Raid-Z Configuration (7x10(9+1))
#

RANKS=7
CHANNELS=10

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.dragon.example
	udev_raidz_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.dragon.example
}
