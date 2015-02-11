#!/bin/bash
#
# Four disk Raid-0 DM in a single Raid-0 Configuration
#

PVCREATE=${PVCREATE:-/sbin/pvcreate}
PVREMOVE=${PVREMOVE:-/sbin/pvremove}
PVDEVICES=${PVDEVICES:-"/dev/sd[abcd]"}

VGCREATE=${VGCREATE:-/sbin/vgcreate}
VGREMOVE=${VGREMOVE:-/sbin/vgremove}
VGNAME=${VGNAME:-"vg_tank"}

LVCREATE=${LVCREATE:-/sbin/lvcreate}
LVREMOVE=${LVREMOVE:-/sbin/lvremove}
LVNAME=${LVNAME:-"lv_tank"}
LVSTRIPES=${LVSTRIPES:-4}
LVSIZE=${LVSIZE:-32G}

DEVICES="/dev/${VGNAME}/${LVNAME}"

zpool_dm_destroy() {
	msg ${LVREMOVE} -f ${VGNAME}/${LVNAME}
	${LVREMOVE} -f ${VGNAME}/${LVNAME} >/dev/null

	msg ${VGREMOVE} -f ${VGNAME}
	${VGREMOVE} -f ${VGNAME} >/dev/null

	msg ${PVREMOVE} ${PVDEVICES}
	${PVREMOVE} ${PVDEVICES} >/dev/null
}

zpool_create() {
	# Remove EFI labels which cause pvcreate failure
	for DEVICE in ${PVDEVICES}; do
		dd if=/dev/urandom of=${DEVICE} bs=1k count=32 &>/dev/null
	done

	msg ${PVCREATE} -f ${PVDEVICES}
	${PVCREATE} -f ${PVDEVICES} >/dev/null || exit 1

	msg ${VGCREATE} ${VGNAME} ${PVDEVICES}
	${VGCREATE} ${VGNAME} ${PVDEVICES} >/dev/null || exit 2

	msg ${LVCREATE} --size=${LVSIZE} --stripes=${LVSTRIPES} \
		--name=${LVNAME} ${VGNAME}
	${LVCREATE} --size=${LVSIZE} --stripes=${LVSTRIPES} \
		--name=${LVNAME} ${VGNAME} >/dev/null || exit 3

	msg ${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${DEVICES}
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} \
		${DEVICES} || (zpool_dm_destroy && exit 4)
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	zpool_dm_destroy
}
