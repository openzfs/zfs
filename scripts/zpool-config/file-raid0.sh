#!/bin/bash
#
# 4 File Raid-0 Configuration
#

FILEDIR=${FILEDIR:-/var/tmp}
FILES=${FILES:-"$FILEDIR/file-vdev0 $FILEDIR/file-vdev1 \
    $FILEDIR/file-vdev2 $FILEDIR/file-vdev3"}

zpool_create() {
	for FILE in ${FILES}; do
		msg "Creating ${FILE}"
		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1024k count=0 seek=256 \
			&>/dev/null || die "Error $? creating ${FILE}"
	done

	msg ${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${FILES}
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${FILES} || exit 1
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}

	for FILE in ${FILES}; do
		msg "Removing ${FILE}"
		rm -f ${FILE} || exit 1
	done
}
