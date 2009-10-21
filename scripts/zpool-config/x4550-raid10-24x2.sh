#!/bin/bash
#
# Sun Fire x4550 (Thumper/Thor) Raid-10 Configuration (24x2(1+1))
#

RANKS=8
CHANNELS=6

zpool_create() {
	udev_setup ${UDEVDIR}/99-zpool.rules.x4550
	udev_raid10_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID10S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID10S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
