#!/bin/sh
#
# Send notification in response to a DATA error.
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
[ -n "${ZED_NOTIFY_DATA}" ] || exit 3

rate_limit_tag="${ZEVENT_POOL};${ZEVENT_VDEV_GUID:-0};${ZEVENT_SUBCLASS};notify"
zed_rate_limit "${rate_limit_tag}" || exit 3

umask 077
note_subject="ZFS ${ZEVENT_SUBCLASS} error for ${ZEVENT_POOL} on $(hostname)"
note_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
{
    echo "ZFS has detected a data error:"
    echo
    echo "   eid: ${ZEVENT_EID}"
    echo " class: ${ZEVENT_SUBCLASS}"
    echo "  host: $(hostname)"
    echo "  time: ${ZEVENT_TIME_STRING}"
    echo " error: ${ZEVENT_ZIO_ERR}"
    echo " objid: ${ZEVENT_ZIO_OBJSET}:${ZEVENT_ZIO_OBJECT}"
    echo "  pool: ${ZEVENT_POOL}"
} > "${note_pathname}"

zed_notify "${note_subject}" "${note_pathname}"; rv=$?
rm -f "${note_pathname}"
exit "${rv}"
