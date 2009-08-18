#!/bin/bash
#
# Dragon (White Box) Raid-Z2 Configuration (15x8 stripes)
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
RAIDZ2_GROUPS=15
RAIDZ2S=()
Z_IDX=0

zpool_create() {
	D_IDX=0
	for i in `seq 1 ${RAIDZ2_GROUPS}`; do
		RAIDZ2=""
		for IDX in `seq 1 ${DEVICES_PER_CTRL}`; do
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_A[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_B[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_C[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_D[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_E[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_F[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_G[${D_IDX}]}`"
			RAIDZ2="${RAIDZ2} `readlink -f ${DEVICES_H[${D_IDX}]}`"
			let D_IDX=D_IDX+1
		done
		RAIDZ2S[${Z_IDX}]="raidz2 ${RAIDZ2}"
		let Z_IDX=Z_IDX+1
	done

	msg ${ZPOOL} create -f ${ZPOOL_NAME} ${RAIDZ2S[*]}
	${ZPOOL} create -f ${ZPOOL_NAME} ${RAIDZ2S[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
