#!/bin/sh
#
# Send email to ZED_EMAIL in response to a RESILVER.FINISH or SCRUB.FINISH.
#
# By default, "zpool status" output will only be included for a scrub.finish
# zevent if the pool is not healthy; to always include its output, set
# ZED_EMAIL_VERBOSE=1.
#
# Exit codes:
#   0: email sent
#   1: email failed
#   2: email not configured
#   3: email suppressed
#   9: internal error

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

[ -n "${ZED_EMAIL}" ] || exit 2

[ -n "${ZEVENT_POOL}" ] || exit 9
[ -n "${ZEVENT_SUBCLASS}" ] || exit 9

if   [ "${ZEVENT_SUBCLASS}" = "resilver.finish" ]; then
    action="resilver"
elif [ "${ZEVENT_SUBCLASS}" = "scrub.finish" ]; then
    action="scrub"
else
    zed_log_err "unsupported event class \"${ZEVENT_SUBCLASS}\""
    exit 9
fi

zed_check_cmd "mail" "${ZPOOL}" || exit 9

# For scrub, suppress email if pool is healthy and verbosity is not enabled.
#
if [ "${ZEVENT_SUBCLASS}" = "scrub.finish" ]; then
    healthy="$("${ZPOOL}" status -x "${ZEVENT_POOL}" \
        | grep "'${ZEVENT_POOL}' is healthy")"
    [ -n "${healthy}" ] && [ "${ZED_EMAIL_VERBOSE}" -eq 0 ] && exit 3
fi

umask 077
email_subject="ZFS ${ZEVENT_SUBCLASS} event for ${ZEVENT_POOL} on $(hostname)"
email_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
cat > "${email_pathname}" <<EOF
ZFS has finished a ${action}:

   eid: ${ZEVENT_EID}
  host: $(hostname)
  time: ${ZEVENT_TIME_STRING}
$("${ZPOOL}" status "${ZEVENT_POOL}")
EOF

mail -s "${email_subject}" "${ZED_EMAIL}" < "${email_pathname}"
mail_status=$?

if [ "${mail_status}" -ne 0 ]; then
    zed_log_msg "mail exit=${mail_status}"
    exit 1
fi
rm -f "${email_pathname}"
exit 0
