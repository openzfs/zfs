#!/bin/bash
#
# Dragon (White Box) Raid-0 Configuration (1x70)
#

RANKS=7
CHANNELS=10

zpool_create() {
	udev_setup ${UDEVDIR}/99-zpool.rules.dragon
	udev_raid0_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAID0S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
