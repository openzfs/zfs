#!/bin/sh
#
# Send email to ZED_EMAIL in response to a RESILVER.FINISH zevent.
#
test -f zed.rc && . ./zed.rc

# Only run if ZED_EMAIL has been configured.
test -n "${ZED_EMAIL}" || exit 0

cat <<EOF \
| mail -s "ZFS Resilver Finish for ${ZEVENT_POOL} on `hostname`" ${ZED_EMAIL}
A ZFS pool has finished resilvering:

   eid: ${ZEVENT_EID}
  host: `hostname`
`${ZPOOL} status ${ZEVENT_POOL}`
EOF
exit 0
