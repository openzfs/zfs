#!/bin/bash
#
# Sun Fire x4550 (Thumper) Raid-Z Configuration (6x8 stripe)
#

DEVICES=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000`)
DEVICES_02=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:02`)
DEVICES_03=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:03`)
DEVICES_04=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:04`)
DEVICES_41=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:41`)
DEVICES_42=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:42`)
DEVICES_43=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000\:43`)

DEVICES_PER_CTRL=1
Z_IDX=0
RAIDZS=()

zpool_create() {

	D_IDX=0
	while [ ${D_IDX} -lt ${#DEVICES_02[@]} ]; do
		RAIDZ=""
		for IDX in `seq 1 ${DEVICES_PER_CTRL}`; do
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_02[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_03[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_04[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_41[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_42[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_43[${D_IDX}]}`"
			let D_IDX=D_IDX+1
		done
		RAIDZS[${Z_IDX}]="raidz ${RAIDZ}"
		let Z_IDX=Z_IDX+1
	done

	msg ${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${RAIDZS[*]}
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
}
