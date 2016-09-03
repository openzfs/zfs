#!/bin/sh
#
# Log the zevent via syslog.

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

zed_log_msg "eid=${ZEVENT_EID}" "class=${ZEVENT_SUBCLASS}" \
    "${ZEVENT_POOL:+"pool=${ZEVENT_POOL}"}" \
    "${ZEVENT_VDEV_STATE_STR:+"vdev_state=${ZEVENT_VDEV_STATE_STR}"}"
exit 0
