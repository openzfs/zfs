#!/bin/sh
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License Version 1.0 (CDDL-1.0).
# You can obtain a copy of the license from the top-level file
# "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
# You may not use this file except in compliance with the license.
#
# CDDL HEADER END
#

#
# Send notification in response to a fault induced statechange
#
# ZEVENT_SUBCLASS: 'statechange'
# ZEVENT_VDEV_STATE_STR: 'DEGRADED', 'FAULTED', 'REMOVED', or 'UNAVAIL'
#
# Exit codes:
#   0: notification sent
#   1: notification failed
#   2: notification not configured
#   3: statechange not relevant
#   4: statechange string missing (unexpected)

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

[ -n "${ZEVENT_VDEV_STATE_STR}" ] || exit 4

if [ "${ZEVENT_VDEV_STATE_STR}" != "FAULTED" ] \
        && [ "${ZEVENT_VDEV_STATE_STR}" != "DEGRADED" ] \
        && [ "${ZEVENT_VDEV_STATE_STR}" != "REMOVED" ] \
        && [ "${ZEVENT_VDEV_STATE_STR}" != "UNAVAIL" ]; then
    exit 3
fi

umask 077
note_subject="ZFS device fault for pool ${ZEVENT_POOL} on $(hostname)"
note_pathname="$(mktemp)"
{
    if [ "${ZEVENT_VDEV_STATE_STR}" = "FAULTED" ] ; then
        echo "The number of I/O errors associated with a ZFS device exceeded"
        echo "acceptable levels. ZFS has marked the device as faulted."
    elif [ "${ZEVENT_VDEV_STATE_STR}" = "DEGRADED" ] ; then
        echo "The number of checksum errors associated with a ZFS device"
        echo "exceeded acceptable levels. ZFS has marked the device as"
        echo "degraded."
    else
        echo "ZFS has detected that a device was removed."
    fi

    echo
    echo " impact: Fault tolerance of the pool may be compromised."
    echo "    eid: ${ZEVENT_EID}"
    echo "  class: ${ZEVENT_SUBCLASS}"
    echo "  state: ${ZEVENT_VDEV_STATE_STR}"
    echo "   host: $(hostname)"
    echo "   time: ${ZEVENT_TIME_STRING}"

    [ -n "${ZEVENT_VDEV_TYPE}" ] && echo "  vtype: ${ZEVENT_VDEV_TYPE}"
    [ -n "${ZEVENT_VDEV_PATH}" ] && echo "  vpath: ${ZEVENT_VDEV_PATH}"
    [ -n "${ZEVENT_VDEV_PHYSPATH}" ] && echo "  vphys: ${ZEVENT_VDEV_PHYSPATH}"
    [ -n "${ZEVENT_VDEV_GUID}" ] && echo "  vguid: ${ZEVENT_VDEV_GUID}"
    [ -n "${ZEVENT_VDEV_DEVID}" ] && echo "  devid: ${ZEVENT_VDEV_DEVID}"

    echo "   pool: ${ZEVENT_POOL} (${ZEVENT_POOL_GUID})"

} > "${note_pathname}"

zed_notify "${note_subject}" "${note_pathname}"; rv=$?

rm -f "${note_pathname}"
exit "${rv}"
