#!/bin/bash
#
# Four disk Raid-5 in a single Raid-0 Configuration
#

MDADM=${MDADM:-/sbin/mdadm}
MDDEVICES=${MDDEVICES:-"/dev/sd[abcd]"}
MDCOUNT=${MDCOUNT:-4}
MDRAID=${MDRAID:-5}

DEVICES="/dev/md0"

zpool_create() {
	msg ${MDADM} --create ${DEVICES} --level=${MDRAID} \
		--raid-devices=${MDCOUNT} ${MDDEVICES}
	${MDADM} --create ${DEVICES} --level=${MDRAID} \
		--raid-devices=${MDCOUNT} ${MDDEVICES} || exit 1

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${DEVICES}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} ${DEVICES} || exit 2
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME} || exit 1

	msg ${MDADM} --manage --stop ${DEVICES}
	${MDADM} --manage --stop ${DEVICES} || exit 2

	msg ${MDADM} --zero-superblock ${MDDEVICES}
	${MDADM} --zero-superblock ${MDDEVICES} || exit 3
}
