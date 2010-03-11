#!/bin/bash
#
# Supermicro (White Box) Raid-Z2 Configuration (4x4(2+2))
#

RANKS=4
CHANNELS=4

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.supermicro.example
	udev_raidz2_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.supermicro.example
}
