#!/bin/sh
# shellcheck disable=SC2154
#
# Log the zevent via syslog.
#

# Given POOL and DATASET name for ZVOL
# DEVICE_NAME  for /dev/disk*
# RAW_DEVICE_NAME for /dev/rdisk*
# Create symlink in
# /var/run/zfs/zvol/dsk/POOL/DATASET -> /dev/disk*
# /var/run/zfs/zvol/rdsk/POOL/DATASET -> /dev/rdisk*

ZVOL_ROOT="/var/run/zfs/zvol"

rm -f "${ZVOL_ROOT}/rdsk/${ZEVENT_POOL}/${ZEVENT_VOLUME}" "${ZVOL_ROOT}/dsk/${ZEVENT_POOL}/${ZEVENT_VOLUME}"
rmdir "$(dirname "${ZVOL_ROOT}/rdsk/${ZEVENT_POOL}/${ZEVENT_VOLUME}")" "$(dirname "${ZVOL_ROOT}/dsk/${ZEVENT_POOL}/${ZEVENT_VOLUME}")"

logger -t "${ZED_SYSLOG_TAG:=zed}" -p "${ZED_SYSLOG_PRIORITY:=daemon.notice}" \
	eid="${ZEVENT_EID}" class="${ZEVENT_SUBCLASS}" \
	"${ZEVENT_POOL:+pool=$ZEVENT_POOL}/${ZEVENT_VOLUME} removed symlink"

echo 0
