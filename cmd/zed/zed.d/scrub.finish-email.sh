#!/bin/sh
#
# Send email to ZED_EMAIL in response to a RESILVER.FINISH or SCRUB.FINISH.
# By default, "zpool status" output will only be included in the email for
#   a scrub.finish zevent if the pool is not healthy; to always include its
#   output, set ZED_EMAIL_VERBOSE=1.
# Exit codes:
#   0: email sent
#   1: email failed
#   2: email suppressed
#   3: missing executable
#   4: unsupported event class
#   5: internal error
#
test -f "${ZED_ZEDLET_DIR}/zed.rc" && . "${ZED_ZEDLET_DIR}/zed.rc"

test -n "${ZEVENT_POOL}" || exit 5
test -n "${ZEVENT_SUBCLASS}" || exit 5

if   test "${ZEVENT_SUBCLASS}" = "resilver.finish"; then
  ACTION="resilvering"
elif test "${ZEVENT_SUBCLASS}" = "scrub.finish"; then
  ACTION="scrubbing"
else
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: unsupported event class \"${ZEVENT_SUBCLASS}\"
  exit 4
fi

# Only send email if ZED_EMAIL has been configured.
test -n "${ZED_EMAIL}" || exit 2

# Ensure requisite executables are installed.
if ! command -v "${MAIL:=mail}" >/dev/null 2>&1; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${MAIL}" not installed
  exit 3
fi
if ! test -x "${ZPOOL}"; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${ZPOOL}" not installed
  exit 3
fi

# For scrub, suppress email if pool is healthy and verbosity is not enabled.
if test "${ZEVENT_SUBCLASS}" = "scrub.finish"; then
  HEALTHY=`"${ZPOOL}" status -x "${ZEVENT_POOL}" | \
    grep "'${ZEVENT_POOL}' is healthy"`
  test -n "${HEALTHY}" -a "${ZED_EMAIL_VERBOSE:=0}" = 0 && exit 2
fi

"${MAIL}" -s "ZFS ${ZEVENT_SUBCLASS} event for ${ZEVENT_POOL} on `hostname`" \
  "${ZED_EMAIL}" <<EOF
A ZFS pool has finished ${ACTION}:

   eid: ${ZEVENT_EID}
  host: `hostname`
  time: ${ZEVENT_TIME_STRING}
`"${ZPOOL}" status "${ZEVENT_POOL}"`
EOF
MAIL_STATUS=$?

if test "${MAIL_STATUS}" -ne 0; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${MAIL}" exit="${MAIL_STATUS}"
  exit 1
fi

exit 0
