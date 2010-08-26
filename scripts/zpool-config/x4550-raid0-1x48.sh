#!/bin/bash
#
# Sun Fire x4550 (Thumper/Thor) Raid-0 Configuration (1x48)
#

RANKS=8
CHANNELS=6

zpool_create() {
	udev_setup ${ETCDIR}/zfs/zdev.conf.x4550.example
	udev_raid0_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	udev_cleanup ${ETCDIR}/zfs/zdev.conf.x4550.example
}
