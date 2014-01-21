#!/bin/sh
#
# Send email to ZED_EMAIL in response to a SCRUB.FINISH zevent.
#
test -f zed.rc && . ./zed.rc

# Only run if ZED_EMAIL has been configured.
test -n "${ZED_EMAIL}" || exit 0

HEALTHY=`${ZPOOL} status -x ${ZEVENT_POOL} | head -1 | egrep 'is healthy$'`
test -n "${HEALTHY}" -a "${ZED_EMAIL_VERBOSE:=0}" = 0 && exit 0

cat <<EOF \
| mail -s "ZFS Scrub Finish for ${ZEVENT_POOL} on `hostname`" ${ZED_EMAIL}
A ZFS pool has finished scrubbing:

   eid: ${ZEVENT_EID}
  host: `hostname`
`${ZPOOL} status ${ZEVENT_POOL}`
EOF
exit 0
