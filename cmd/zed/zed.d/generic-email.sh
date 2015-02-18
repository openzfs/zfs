#!/bin/sh
#
# Send email to ZED_EMAIL in response to a given zevent.
#
# This is a generic script than can be symlinked to a file in the
# enabled-zedlets directory to have an email sent when a particular class of
# zevents occurs.  The symlink filename must begin with the zevent (sub)class
# string (e.g., "probe_failure-email.sh" for the "probe_failure" subclass).
# Refer to the zed(8) manpage for details.
#
# Exit codes:
#   0: email sent
#   1: email failed
#   2: email not configured
#   3: email suppressed

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

[ -n "${ZED_EMAIL}" ] || exit 2

# Rate-limit the message based in part on the filename.
#
rate_limit_tag="${ZEVENT_POOL};${ZEVENT_SUBCLASS};$(basename -- "$0")"
rate_limit_interval="${ZED_EMAIL_INTERVAL_SECS}"
zed_rate_limit "${rate_limit_tag}" "${rate_limit_interval}" || exit 3

umask 077
pool_str="${ZEVENT_POOL:+" for ${ZEVENT_POOL}"}"
host_str=" on $(hostname)"
email_subject="ZFS ${ZEVENT_SUBCLASS} event${pool_str}${host_str}"
email_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
{
    echo "ZFS has posted the following event:"
    echo
    echo "   eid: ${ZEVENT_EID}"
    echo " class: ${ZEVENT_SUBCLASS}"
    echo "  host: $(hostname)"
    echo "  time: ${ZEVENT_TIME_STRING}"

    if [ -n "${ZEVENT_VDEV_PATH}" ]; then
        echo " vpath: ${ZEVENT_VDEV_PATH}"
        [ -n "${ZEVENT_VDEV_TYPE}" ] && echo " vtype: ${ZEVENT_VDEV_TYPE}"
    fi

    [ -n "${ZEVENT_POOL}" ] && [ -x "${ZPOOL}" ] \
        && "${ZPOOL}" status "${ZEVENT_POOL}"

} > "${email_pathname}"

mail -s "${email_subject}" "${ZED_EMAIL}" < "${email_pathname}"
mail_status=$?

if [ "${mail_status}" -ne 0 ]; then
    zed_log_msg "mail exit=${mail_status}"
    exit 1
fi
rm -f "${email_pathname}"
exit 0
