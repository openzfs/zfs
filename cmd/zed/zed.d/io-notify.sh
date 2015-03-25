#!/bin/sh
#
# Send notification in response to a CHECKSUM, DATA, or IO error.
#
# Only one notification per ZED_NOTIFY_INTERVAL_SECS will be sent for a given
# class/pool/[vdev] combination.  This protects against spamming the recipient
# should multiple events occur together in time for the same pool/[vdev].
#
# Exit codes:
#   0: notification sent
#   1: notification failed
#   2: notification not configured
#   3: notification suppressed
#   9: internal error

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

[ -n "${ZEVENT_POOL}" ] || exit 9
[ -n "${ZEVENT_SUBCLASS}" ] || exit 9

if [ "${ZEVENT_SUBCLASS}" != "checksum" ] \
        && [ "${ZEVENT_SUBCLASS}" != "data" ] \
        && [ "${ZEVENT_SUBCLASS}" != "io" ]; then
    zed_log_err "unsupported event class \"${ZEVENT_SUBCLASS}\""
    exit 9
fi

rate_limit_tag="${ZEVENT_POOL};${ZEVENT_VDEV_GUID:-0};${ZEVENT_SUBCLASS};notify"
zed_rate_limit "${rate_limit_tag}" || exit 3

umask 077
note_subject="ZFS ${ZEVENT_SUBCLASS} error for ${ZEVENT_POOL} on $(hostname)"
note_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
{
    [ "${ZEVENT_SUBCLASS}" = "io" ] && article="an" || article="a"

    echo "ZFS has detected ${article} ${ZEVENT_SUBCLASS} error:"
    echo
    echo "   eid: ${ZEVENT_EID}"
    echo " class: ${ZEVENT_SUBCLASS}"
    echo "  host: $(hostname)"
    echo "  time: ${ZEVENT_TIME_STRING}"

    [ -n "${ZEVENT_VDEV_TYPE}" ] && echo " vtype: ${ZEVENT_VDEV_TYPE}"
    [ -n "${ZEVENT_VDEV_PATH}" ] && echo " vpath: ${ZEVENT_VDEV_PATH}"
    [ -n "${ZEVENT_VDEV_GUID}" ] && echo " vguid: ${ZEVENT_VDEV_GUID}"

    [ -n "${ZEVENT_VDEV_CKSUM_ERRORS}" ] \
        && echo " cksum: ${ZEVENT_VDEV_CKSUM_ERRORS}"

    [ -n "${ZEVENT_VDEV_READ_ERRORS}" ] \
        && echo "  read: ${ZEVENT_VDEV_READ_ERRORS}"

    [ -n "${ZEVENT_VDEV_WRITE_ERRORS}" ] \
        && echo " write: ${ZEVENT_VDEV_WRITE_ERRORS}"

    echo "  pool: ${ZEVENT_POOL}"

} > "${note_pathname}"

zed_notify "${note_subject}" "${note_pathname}"; rv=$?
rm -f "${note_pathname}"
exit "${rv}"
