#!/bin/bash
#
# Dragon (White Box) Raid-Z Configuration (15x8 stripes)
#

DEVICES_A=(`ls /dev/disk/by-path/* | grep pci-0000\:03 | head -15`)
DEVICES_B=(`ls /dev/disk/by-path/* | grep pci-0000\:03 | tail -15`)
DEVICES_C=(`ls /dev/disk/by-path/* | grep pci-0000\:04 | head -15`)
DEVICES_D=(`ls /dev/disk/by-path/* | grep pci-0000\:04 | tail -15`)
DEVICES_E=(`ls /dev/disk/by-path/* | grep pci-0000\:83 | head -15`)
DEVICES_F=(`ls /dev/disk/by-path/* | grep pci-0000\:83 | tail -15`)
DEVICES_G=(`ls /dev/disk/by-path/* | grep pci-0000\:84 | head -15`)
DEVICES_H=(`ls /dev/disk/by-path/* | grep pci-0000\:84 | tail -15`)

DEVICES_PER_CTRL=1
RAIDZ_GROUPS=15
RAIDZS=()
Z_IDX=0

zpool_create() {
	D_IDX=0
	for i in `seq 1 ${RAIDZ_GROUPS}`; do
		RAIDZ=""
		for IDX in `seq 1 ${DEVICES_PER_CTRL}`; do
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_A[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_B[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_C[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_D[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_E[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_F[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_G[${D_IDX}]}`"
			RAIDZ="${RAIDZ} `readlink -f ${DEVICES_H[${D_IDX}]}`"
			let D_IDX=D_IDX+1
		done
		RAIDZS[${Z_IDX}]="raidz ${RAIDZ}"
		let Z_IDX=Z_IDX+1
	done

	msg ${ZPOOL} create -f ${ZPOOL_NAME} ${RAIDZS[*]}
	${ZPOOL} create -f ${ZPOOL_NAME} ${RAIDZS[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
