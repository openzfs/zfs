#!/bin/sh
# shellcheck disable=SC2154
#
# Send notification in response to a RESILVER_FINISH or SCRUB_FINISH.
#
# By default, "zpool status" output will only be included for a scrub_finish
# zevent if the pool is not healthy; to always include its output, set
# ZED_NOTIFY_VERBOSE=1.
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

if   [ "${ZEVENT_SUBCLASS}" = "resilver_finish" ]; then
    action="resilver"
elif [ "${ZEVENT_SUBCLASS}" = "scrub_finish" ]; then
    action="scrub"
else
    zed_log_err "unsupported event class \"${ZEVENT_SUBCLASS}\""
    exit 9
fi

zed_check_cmd "${ZPOOL}" || exit 9

# For scrub, suppress notification if the pool is healthy
# and verbosity is not enabled.
#
if [ "${ZEVENT_SUBCLASS}" = "scrub_finish" ]; then
    healthy="$("${ZPOOL}" status -x "${ZEVENT_POOL}" \
        | grep "'${ZEVENT_POOL}' is healthy")"
    [ -n "${healthy}" ] && [ "${ZED_NOTIFY_VERBOSE}" -eq 0 ] && exit 3
fi

umask 077
note_subject="ZFS ${ZEVENT_SUBCLASS} event for ${ZEVENT_POOL} on $(hostname)"
note_pathname="$(mktemp)"
{
    echo "ZFS has finished a ${action}:"
    echo
    echo "   eid: ${ZEVENT_EID}"
    echo " class: ${ZEVENT_SUBCLASS}"
    echo "  host: $(hostname)"
    echo "  time: ${ZEVENT_TIME_STRING}"

    "${ZPOOL}" status "${ZEVENT_POOL}"

} > "${note_pathname}"

zed_notify "${note_subject}" "${note_pathname}"; rv=$?
rm -f "${note_pathname}"
exit "${rv}"
