#!/bin/bash
#
# Dragon (White Box) Raid-Z2 Configuration (7x10(8+2))
#

RANKS=7
CHANNELS=10

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.dragon.example
	udev_raidz2_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.dragon.example
}
