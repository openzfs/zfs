#!/bin/bash
#
# Supermicro (White Box) Raid-0 Configuration (1x16)
#

RANKS=4
CHANNELS=4

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.supermicro.example
	udev_raid0_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.supermicro.example
}
