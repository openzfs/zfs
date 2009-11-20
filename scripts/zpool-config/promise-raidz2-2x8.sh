#!/bin/bash
#
# Flash (White Box) Raid-Z2 Configuration (2x8(6+2))
#

RANKS=8
CHANNELS=2

zpool_create() {
	udev_setup ${UDEVDIR}/99-zpool.rules.promise
	udev_raidz2_setup ${RANKS} ${CHANNELS}

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${RAIDZ2S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
