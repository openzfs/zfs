#!/bin/sh
#
# Send email to ZED_EMAIL in response to a CHECKSUM or IO zevent.
# Only one message per ZED_EMAIL_INTERVAL_SECS will be sent for a given
#   class/pool/vdev combination.  This protects against spamming the recipient
#   should multiple events occur together in time for the same pool/device.
# Exit codes:
#   0: email sent
#   1: email failed
#   2: email suppressed
#   3: missing executable
#   4: unsupported event class
#   5: internal error
# State File Format:
#   POOL;VDEV_PATH;TIME_OF_LAST_EMAIL
#
test -f "${ZED_ZEDLET_DIR}/zed.rc" && . "${ZED_ZEDLET_DIR}/zed.rc"

test -n "${ZEVENT_POOL}" || exit 5
test -n "${ZEVENT_SUBCLASS}" || exit 5
test -n "${ZEVENT_VDEV_PATH}" || exit 5

if test "${ZEVENT_SUBCLASS}" != "checksum" \
    -a  "${ZEVENT_SUBCLASS}" != "io"; then
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

NAME="zed.${ZEVENT_SUBCLASS}.email"
LOCKFILE="${ZED_LOCKDIR:=/var/lock}/${NAME}.lock"
STATEFILE="${ZED_RUNDIR:=/var/run}/${NAME}.state"

# Obtain lock to ensure mutual exclusion for accessing state.
exec 8> "${LOCKFILE}"
flock -x 8

# Query state for last time email was sent for this pool/vdev.
TIME_NOW=`date +%s`
TIME_LAST=`egrep "^${ZEVENT_POOL};${ZEVENT_VDEV_PATH};" "${STATEFILE}" \
  2>/dev/null | cut -d ";" -f3`
if test -n "${TIME_LAST}"; then
  TIME_DELTA=`expr "${TIME_NOW}" - "${TIME_LAST}"`
  if test "${TIME_DELTA}" -lt "${ZED_EMAIL_INTERVAL_SECS:=3600}"; then
    exit 2
  fi
fi

"${MAIL}" -s "ZFS ${ZEVENT_SUBCLASS} error for ${ZEVENT_POOL} on `hostname`" \
  "${ZED_EMAIL}" <<EOF
A ZFS ${ZEVENT_SUBCLASS} error has been detected:

   eid: ${ZEVENT_EID}
  host: `hostname`
  time: ${ZEVENT_TIME_STRING}
  pool: ${ZEVENT_POOL}
  vdev: ${ZEVENT_VDEV_TYPE}:${ZEVENT_VDEV_PATH}
EOF
MAIL_STATUS=$?

# Update state.
egrep -v "^${ZEVENT_POOL};${ZEVENT_VDEV_PATH};" "${STATEFILE}" \
  2>/dev/null > "${STATEFILE}.$$"
echo "${ZEVENT_POOL};${ZEVENT_VDEV_PATH};${TIME_NOW}" >> "${STATEFILE}.$$"
mv -f "${STATEFILE}.$$" "${STATEFILE}"

if test "${MAIL_STATUS}" -ne 0; then
  logger -t "${ZED_SYSLOG_TAG:=zed}" \
    -p "${ZED_SYSLOG_PRIORITY:=daemon.warning}" \
    `basename "$0"`: "${MAIL}" exit="${MAIL_STATUS}"
  exit 1
fi

exit 0
