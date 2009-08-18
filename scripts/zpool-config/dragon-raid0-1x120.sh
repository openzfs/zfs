#!/bin/bash
#
# Dragon (White Box) Raid-0 Configuration (1x120)
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

	msg ${ZPOOL} create -f ${ZPOOL_NAME} ${STRIPES[*]}
	${ZPOOL} create -f ${ZPOOL_NAME} ${STRIPES[*]} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
}
