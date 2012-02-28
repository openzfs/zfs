#!/usr/bin/env bash
#
# 4 File Raid-Z2 Configuration
#

FILES="/tmp/zpool-vdev0  \
       /tmp/zpool-vdev1  \
       /tmp/zpool-vdev2  \
       /tmp/zpool-vdev3"

zpool_create() {
	for FILE in ${FILES}; do
		msg "Creating ${FILE}"
		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=0 seek=256 \
			&>/dev/null || die "Error $? creating ${FILE}"
	done

	msg ${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} raidz2 ${FILES}
	${ZPOOL} create ${FORCE_FLAG} ${ZPOOL_NAME} raidz2 ${FILES} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		msg "Removing ${FILE}"
		rm -f ${FILE} || exit 1
	done
}
