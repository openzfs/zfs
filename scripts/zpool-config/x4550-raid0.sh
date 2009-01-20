#!/bin/bash
#
# Sun Fire x4550 (Thumper) Raid-0 Configuration
#

DEVICES="/dev/sda  /dev/sdb  /dev/sdc  /dev/sdd  /dev/sde  /dev/sdf  \
         /dev/sdg  /dev/sdh  /dev/sdi  /dev/sdj  /dev/sdk  /dev/sdl  \
         /dev/sdm  /dev/sdn  /dev/sdo  /dev/sdp  /dev/sdq  /dev/sdr  \
         /dev/sds  /dev/sdt  /dev/sdu  /dev/sdv  /dev/sdw  /dev/sdx  \
         /dev/sdy  /dev/sdz  /dev/sdaa /dev/sdab /dev/sdac /dev/sdad \
         /dev/sdae /dev/sdaf /dev/sdag /dev/sdah /dev/sdai /dev/sdaj \
         /dev/sdak /dev/sdal /dev/sdam /dev/sdan /dev/sdao /dev/sdap \
         /dev/sdaq /dev/sdar /dev/sdas /dev/sdat /dev/sdau /dev/sdav"

zpool_create() {
	msg ${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES}
	${CMDDIR}/zpool/zpool create -f ${ZPOOL_NAME} ${DEVICES} || exit 1
}

zpool_destroy() {
	msg ${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
	${CMDDIR}/zpool/zpool destroy ${ZPOOL_NAME}
}
