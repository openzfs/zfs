#!/bin/bash
#
# Sun Fire x4550 (Thumper/Thor) Raid-Z Configuration (8x6(4+2))
#

RANKS=8
CHANNELS=6

zpool_create() {
	udev_setup ${UDEVDIR}/99-zpool.rules.x4550
	udev_raidz2_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
