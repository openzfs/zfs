#!/bin/sh
#
# Send notification in response to a given zevent.
#
# This is a generic script than can be symlinked to a file in the
# enabled-zedlets directory to have a notification sent when a particular
# class of zevents occurs.  The symlink filename must begin with the zevent
# (sub)class string (e.g., "probe_failure-notify.sh" for the "probe_failure"
# subclass).  Refer to the zed(8) manpage for details.
#
# Only one notification per ZED_NOTIFY_INTERVAL_SECS will be sent for a given
# class/pool combination.  This protects against spamming the recipient
# should multiple events occur together in time for the same pool.
#
# Exit codes:
#   0: notification sent
#   1: notification failed
#   2: notification not configured
#   3: notification suppressed

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

# Rate-limit the notification based in part on the filename.
#
rate_limit_tag="${ZEVENT_POOL};${ZEVENT_SUBCLASS};${0##*/}"
rate_limit_interval="${ZED_NOTIFY_INTERVAL_SECS}"
zed_rate_limit "${rate_limit_tag}" "${rate_limit_interval}" || exit 3

umask 077
pool_str="${ZEVENT_POOL:+" for ${ZEVENT_POOL}"}"
host_str=" on $(hostname)"
note_subject="ZFS ${ZEVENT_SUBCLASS} event${pool_str}${host_str}"
note_pathname="$(mktemp)"
{
    echo "ZFS has posted the following event:"
    echo
    echo "   eid: ${ZEVENT_EID}"
    echo " class: ${ZEVENT_SUBCLASS}"
    echo "  host: $(hostname)"
    echo "  time: ${ZEVENT_TIME_STRING}"

    [ -n "${ZEVENT_VDEV_TYPE}" ] && echo " vtype: ${ZEVENT_VDEV_TYPE}"
    [ -n "${ZEVENT_VDEV_PATH}" ] && echo " vpath: ${ZEVENT_VDEV_PATH}"
    [ -n "${ZEVENT_VDEV_GUID}" ] && echo " vguid: ${ZEVENT_VDEV_GUID}"

    [ -n "${ZEVENT_POOL}" ] && [ -x "${ZPOOL}" ] \
        && "${ZPOOL}" status "${ZEVENT_POOL}"

} > "${note_pathname}"

zed_notify "${note_subject}" "${note_pathname}"; rv=$?
rm -f "${note_pathname}"
exit "${rv}"
