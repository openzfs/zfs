#!/bin/sh
#
# Send email to ZED_EMAIL in response to a given zevent.
# This is a generic script than can be symlinked to a file in the zed
#   enabled-scripts directory in order to have email sent when a particular
#   class of zevents occurs.  The symlink filename must begin with the zevent
#   (sub)class string (eg, "probe_failure-email.sh" for the "probe_failure"
#   subclass).  Refer to the zed(8) manpage for details.
# Exit codes:
#   0: email sent
#   1: email failed
#   2: email suppressed
#   3: missing executable
#
test -f "${ZED_SCRIPT_DIR}/zed.rc" && . "${ZED_SCRIPT_DIR}/zed.rc"

# Only send email if ZED_EMAIL has been configured.
test -n "${ZED_EMAIL}" || exit 2

# Ensure requisite executables are installed.
if ! command -v "${MAIL:=mail}" >/dev/null 2>&1; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${MAIL}" not installed
  exit 3
fi

# Override the default umask to restrict access to the msgbody tmpfile.
umask 077

SUBJECT="ZFS ${ZEVENT_SUBCLASS} event"
test -n "${ZEVENT_POOL}" && SUBJECT="${SUBJECT} for ${ZEVENT_POOL}"
SUBJECT="${SUBJECT} on `hostname`"

MSGBODY="${TMPDIR:=/tmp}/`basename \"$0\"`.$$"
{
  echo "A ZFS ${ZEVENT_SUBCLASS} event has been posted:"
  echo
  echo "   eid: ${ZEVENT_EID}"
  echo "  host: `hostname`"
  echo "  time: ${ZEVENT_TIME_STRING}"
  test -n "${ZEVENT_VDEV_TYPE}" -a -n "${ZEVENT_VDEV_PATH}" && \
    echo "  vdev: ${ZEVENT_VDEV_TYPE}:${ZEVENT_VDEV_PATH}"
  test -n "${ZEVENT_POOL}" -a -x "${ZPOOL}" && \
    "${ZPOOL}" status "${ZEVENT_POOL}"
} > "${MSGBODY}"

test -f "${MSGBODY}" && "${MAIL}" -s "${SUBJECT}" "${ZED_EMAIL}" < "${MSGBODY}"
MAIL_STATUS=$?
rm -f "${MSGBODY}"

if test "${MAIL_STATUS}" -ne 0; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${MAIL}" exit="${MAIL_STATUS}"
  exit 1
fi

exit 0
