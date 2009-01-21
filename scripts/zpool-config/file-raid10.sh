#!/bin/bash
#
# 4 File Raid-10 Configuration
#

FILES_M1="/tmp/zpool-vdev0  \
          /tmp/zpool-vdev1"
FILES_M2="/tmp/zpool-vdev2  \
          /tmp/zpool-vdev3"
FILES="${FILES_M1} ${FILES_M2}"

zpool_create() {
	for FILE in ${FILES}; do
		msg "Creating ${FILE}"
		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=256 &>/dev/null ||
			die "Error $? creating ${FILE}"
	done

	msg ${CMDDIR}/zpool/zpool create ${ZPOOL_NAME} \
		mirror ${FILES_M1} mirror ${FILES_M2}
	${CMDDIR}/zpool/zpool create ${ZPOOL_NAME} \
		mirror ${FILES_M1} mirror ${FILES_M2} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		msg "Removing ${FILE}"
		rm -f ${FILE} || exit 1
	done
}
