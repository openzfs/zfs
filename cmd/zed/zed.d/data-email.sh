#!/bin/sh
#
# Send email to ZED_EMAIL in response to a DATA error.
#
# Only one email per ZED_EMAIL_INTERVAL_SECS will be sent for a given
# class/pool combination.  This protects against spamming the recipient
# should multiple events occur together in time for the same pool.
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

if [ "${ZEVENT_SUBCLASS}" != "data" ]; then \
    zed_log_err "unsupported event class \"${ZEVENT_SUBCLASS}\""
    exit 9
fi

zed_check_cmd "mail" || exit 9

zed_rate_limit "${ZEVENT_POOL};${ZEVENT_SUBCLASS};email" || exit 3

umask 077
email_subject="ZFS ${ZEVENT_SUBCLASS} error for ${ZEVENT_POOL} on $(hostname)"
email_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
cat > "${email_pathname}" <<EOF
A ZFS ${ZEVENT_SUBCLASS} error has been detected:

   eid: ${ZEVENT_EID}
  host: $(hostname)
  time: ${ZEVENT_TIME_STRING}
  pool: ${ZEVENT_POOL}
EOF

mail -s "${email_subject}" "${ZED_EMAIL}" < "${email_pathname}"
mail_status=$?

if [ "${mail_status}" -ne 0 ]; then
    zed_log_msg "mail exit=${mail_status}"
    exit 1
fi
rm -f "${email_pathname}"
exit 0
