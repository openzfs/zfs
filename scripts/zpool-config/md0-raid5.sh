#!/bin/bash
#
# Four disk Raid-5 in a single Raid-0 Configuration
#

MDADM=${MDADM:-/sbin/mdadm}
MDDEVICES=${MDDEVICES:-"/dev/sd[abcd]"}
MDCOUNT=${MDCOUNT:-4}
MDRAID=${MDRAID:-5}

DEVICES="/dev/md0"

zpool_md_destroy() {
	msg ${MDADM} --manage --stop ${DEVICES}
	${MDADM} --manage --stop ${DEVICES} &>/dev/null

	msg ${MDADM} --zero-superblock ${MDDEVICES}
	${MDADM} --zero-superblock ${MDDEVICES} >/dev/null
}

zpool_create() {
	msg ${MDADM} --create ${DEVICES} --level=${MDRAID} \
		--raid-devices=${MDCOUNT} ${MDDEVICES}
	${MDADM} --create ${DEVICES} --level=${MDRAID} \
		--raid-devices=${MDCOUNT} ${MDDEVICES} \
		&>/dev/null || (zpool_md_destroy && exit 1)

	msg ${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${DEVICES}
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} \
		${DEVICES} || (zpool_md_destroy && exit 2)
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	zpool_md_destroy
}
