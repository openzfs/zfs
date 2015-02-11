#!/bin/bash
#
# 4 loopback devices using the md faulty level for easy
# fault injection on top of which is layered raid0 (striped).
#
#     zpool-vdev0    zpool-vdev1    zpool-vdev2    zpool-vdev3
#     loop0          loop1          loop2          loop3
#     md0 (faulty)   md1 (faulty)   md2 (faulty)   md3 (faulty)
#     <--------------------- raid0 zpool --------------------->
#

FILEDIR=${FILEDIR:-/var/tmp}
FILES=${FILES:-"$FILEDIR/file-vdev0 $FILEDIR/file-vdev1 \
    $FILEDIR/file-vdev2 $FILEDIR/file-vdev3"}
LODEVICES=""
MDDEVICES=""

zpool_create() {
	check_loop_utils
	check_md_utils
	check_md_partitionable || die "Error non-partitionable md devices"

	for FILE in ${FILES}; do
		LODEVICE=`unused_loop_device`
		MDDEVICE=`unused_md_device`

		rm -f ${FILE} || exit 1
		dd if=/dev/zero of=${FILE} bs=1M count=0 seek=256 \
			&>/dev/null || die "Error $? creating ${FILE}"

		# Setup the loopback device on the file.
		msg "Creating ${LODEVICE} using ${FILE}"
		${LOSETUP} ${LODEVICE} ${FILE} || \
			die "Error $? creating ${LODEVICE} using ${FILE}"

		LODEVICES="${LODEVICES} ${LODEVICE}"

		# Setup the md device on the loopback device.
		msg "Creating ${MDDEVICE} using ${LODEVICE}"
		${MDADM} --build ${MDDEVICE} --level=faulty                  \
			--raid-devices=1 ${LODEVICE} &>/dev/null ||          \
			(destroy_md_devices "${MDDEVICES}" &&                \
			destroy_loop_devices "${LODEVICES}" &&               \
			die "Error $? creating ${MDDEVICE} using ${LODEVICE}")
		wait_udev ${MDDEVICE} 30 ||                                  \
			(destroy_md_devices "${MDDEVICES}" &&                \
			destroy_loop_devices "${LODEVICES}" &&               \
			die "Error udev never created ${MDDEVICE}")

		# Check if the md device support partitions
		${BLOCKDEV} --rereadpt ${MDDEVICE} 2>/dev/null ||            \
			(destroy_md_devices "${MDDEVICES}" &&                \
			destroy_loop_devices "${LODEVICES}" &&               \
			die "Error ${MDDEVICE} does not support partitions")

		# Create a GPT/EFI partition table for ZFS to use.
		${PARTED} --script ${MDDEVICE} mklabel gpt
		MDDEVICES="${MDDEVICES} ${MDDEVICE}"
	done

	msg ${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${MDDEVICES}
	${ZPOOL} create ${ZPOOL_FLAGS} ${ZPOOL_NAME} ${MDDEVICES} ||          \
		(destroy_md_devices "${MDDEVICES}" &&                        \
		destroy_loop_devices "${LODEVICES}" && exit 1)

	echo "$LODEVICES" >/tmp/zpool-lo.txt
	echo "$MDDEVICES" >/tmp/zpool-md.txt
}

zpool_destroy() {
	msg ${ZPOOL} destroy ${ZPOOL_NAME}
	${ZPOOL} destroy ${ZPOOL_NAME}
	destroy_md_devices "`cat /tmp/zpool-md.txt`"
	destroy_loop_devices "`cat /tmp/zpool-lo.txt`"

	rm -f /tmp/zpool-md.txt /tmp/zpool-lo.txt
}
