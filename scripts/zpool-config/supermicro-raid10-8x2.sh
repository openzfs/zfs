#!/bin/bash
#
# Supermicro (White Box) Raid-10 Configuration (8x2(1+1))
#

RANKS=4
CHANNELS=4

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.supermicro.example
	udev_raid10_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID10S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID10S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.supermicro.example
}
