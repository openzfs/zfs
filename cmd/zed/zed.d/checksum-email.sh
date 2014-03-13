#!/bin/sh
#
# Send email to ZED_EMAIL in response to a CHECKSUM zevent.
# Only one message per ZED_EMAIL_INTERVAL_SECS will be sent for a given
#   pool/vdev combination. This protects against spamming the recipient
#   should multiple checksum events occur together in time on the same device.
# Exit with 0 if the email was sent, or 1 if the email was suppressed due to a
#   similar event having occurred within the configured time interval.
#
# Checksum Email State File Format:
#   pool:vdev_path:time
#
test -f zed.rc && . ./zed.rc

NAME="zed.checksum.email"
LOCKFILE="${ZED_LOCKDIR:=/var/lock}/${NAME}.lock"
STATEFILE="${ZED_RUNDIR:=/var/run}/${NAME}.state"

# Only run if ZED_EMAIL has been configured.
test -n "${ZED_EMAIL}" || exit 0

exec 8> "${LOCKFILE}"
flock -x 8

TIME_NOW=`date +%s`
TIME_LAST=`egrep "^${ZEVENT_POOL}:${ZEVENT_VDEV_PATH}:" "${STATEFILE}" \
  2>/dev/null | cut -d: -f3`
if test -n "${TIME_LAST}"; then
  TIME_DELTA=`expr "${TIME_NOW}" - "${TIME_LAST}"`
  if test "${TIME_DELTA}" -lt "${ZED_EMAIL_INTERVAL_SECS:=3600}"; then
    exit 1
  fi
fi

cat <<EOF \
| mail -s "ZFS Checksum Error for ${ZEVENT_POOL} on `hostname`" ${ZED_EMAIL}
A ZFS checksum error has been detected:

   eid: ${ZEVENT_EID}
  host: `hostname`
  pool: ${ZEVENT_POOL}
  vdev: ${ZEVENT_VDEV_PATH}
  time: ${ZEVENT_TIME_STRING}
EOF

egrep -v "^${ZEVENT_POOL}:${ZEVENT_VDEV_PATH}:" "${STATEFILE}" \
  2>/dev/null > "${STATEFILE}.$$"
echo "${ZEVENT_POOL}:${ZEVENT_VDEV_PATH}:${TIME_NOW}" >> "${STATEFILE}.$$"
mv -f "${STATEFILE}.$$" "${STATEFILE}"
exit 0
