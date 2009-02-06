#!/bin/bash
#
# Sun Fire x4550 (Thumper) Raid-10 Configuration (6x8 mirror)
#

DEVICES=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000`)
DEVICES_02=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:02`)
DEVICES_03=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:03`)
DEVICES_04=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:04`)
DEVICES_41=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:41`)
DEVICES_42=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:42`)
DEVICES_43=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:43`)

M_IDX=0
MIRRORS=()

zpool_create() {

	D_IDX=0
	while [ ${D_IDX} -lt ${#DEVICES_02[@]} ]; do
		MIRROR1=`readlink -f ${DEVICES_02[${D_IDX}]}`
		MIRROR2=`readlink -f ${DEVICES_03[${D_IDX}]}`
		MIRROR3=`readlink -f ${DEVICES_04[${D_IDX}]}`
		MIRROR4=`readlink -f ${DEVICES_41[${D_IDX}]}`
		MIRROR5=`readlink -f ${DEVICES_42[${D_IDX}]}`
		MIRROR6=`readlink -f ${DEVICES_43[${D_IDX}]}`
		MIRRORS[${M_IDX}]="mirror ${MIRROR1} ${MIRROR2} ${MIRROR3} ${MIRROR4} ${MIRROR5} ${MIRROR6}"
		let D_IDX=D_IDX+1
		let M_IDX=M_IDX+1
	done

	msg ${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${MIRRORS[*]}
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${MIRRORS[*]} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
}
