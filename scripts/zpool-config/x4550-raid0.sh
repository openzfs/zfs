#!/bin/bash
#
# Sun Fire x4550 (Thumper) Raid-0 Configuration
#

DEVICES=(`ls /dev/disk/by-path/* | grep -v part | grep pci-0000`)

S_IDX=0
STRIPES=()

zpool_create() {

	while [ ${S_IDX} -lt ${#DEVICES[@]} ]; do
		STRIPE=`readlink -f ${DEVICES[${S_IDX}]}`
		STRIPES[${S_IDX}]="${STRIPE}"
		let S_IDX=S_IDX+1
	done

	msg ${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${STRIPES[*]}
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${STRIPES[*]} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
}
