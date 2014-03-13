#!/bin/sh
#
# Log the zevent via syslog.
#
test -f zed.rc && . ./zed.rc

logger -t "${ZED_SYSLOG_TAG:=zed}" -p "${ZED_SYSLOG_PRIORITY:=daemon.notice}" \
  eid="${ZEVENT_EID}" class="${ZEVENT_SUBCLASS}" \
  "${ZEVENT_POOL:+pool=$ZEVENT_POOL}"
exit 0
