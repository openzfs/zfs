#!/bin/sh
#
# Log the zevent via syslog.

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

zed_exit_if_ignoring_this_event

zed_log_msg "eid=${ZEVENT_EID}" "class=${ZEVENT_SUBCLASS}" \
    "${ZEVENT_POOL_GUID:+"pool_guid=${ZEVENT_POOL_GUID}"}" \
    "${ZEVENT_VDEV_PATH:+"vdev_path=${ZEVENT_VDEV_PATH}"}" \
    "${ZEVENT_VDEV_STATE_STR:+"vdev_state=${ZEVENT_VDEV_STATE_STR}"}"
exit 0
