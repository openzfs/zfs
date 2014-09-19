#!/bin/sh
#
# Replace a device with a hot spare in response to IO or checksum errors.
# The following actions will be performed automatically when the number
# of errors exceed the limit set by ZED_SPARE_ON_IO_ERRORS or
# ZED_SPARE_ON_CHECKSUM_ERRORS.
#
# 1) FAULT the device on IO errors, no futher IO will be attempted.
#    DEGRADE the device on checksum errors, the device is still
#    functional and can be used to service IO requests.
# 2) Set the SES fault beacon for the device.
# 3) Replace the device with a hot spare if any are available.
#
# Once the hot sparing operation is complete either the failed device or
# the hot spare must be manually retired using the 'zpool detach' command.
# The 'autoreplace' functionality which would normally take care of this
# under Illumos has not yet been implemented.
#
# Full support for autoreplace is planned, but it requires that the full
# ZFS Diagnosis Engine be ported.  In the meanwhile this script provides
# the majority of the expected hot spare functionality.
#
# Exit codes:
#  0: replaced by hot spare
#  1: no hot spare device available
#  2: hot sparing disabled
#  3: already faulted or degraded
#  4: unsupported event class
#  5: internal error
#
test -f "${ZED_ZEDLET_DIR}/zed.rc" && . "${ZED_ZEDLET_DIR}/zed.rc"

test -n "${ZEVENT_POOL}" || exit 5
test -n "${ZEVENT_SUBCLASS}" || exit 5
test -n "${ZEVENT_VDEV_PATH}" || exit 5
test -n "${ZEVENT_VDEV_GUID}" || exit 5

# Defaults to disabled, enable in the zed.rc file.
ZED_SPARE_ON_IO_ERRORS=${ZED_SPARE_ON_IO_ERRORS:-0}
ZED_SPARE_ON_CHECKSUM_ERRORS=${ZED_SPARE_ON_CHECKSUM_ERRORS:-0}

if [ ${ZED_SPARE_ON_IO_ERRORS} -eq 0 -a \
     ${ZED_SPARE_ON_CHECKSUM_ERRORS} -eq 0 ]; then
	exit 2
fi

# A lock file is used to serialize execution.
ZED_LOCKDIR=${ZED_LOCKDIR:-/var/lock}
LOCKFILE="${ZED_LOCKDIR}/zed.spare.lock"

exec 8> "${LOCKFILE}"
flock -x 8

# Given a <pool> and <device> return the status, (ONLINE, FAULTED, etc...).
vdev_status() {
	local POOL=$1
	local VDEV=$2
	local T='	'	# tab character since '\t' isn't portable

	${ZPOOL} status ${POOL} | sed -n -e \
	    "s,^[ $T]*\(.*$VDEV\(-part[0-9]\+\)\?\)[ $T]*\([A-Z]\+\).*,\1 \3,p"
	return 0
}

# Fault devices after N I/O errors.
if [ "${ZEVENT_CLASS}" = "ereport.fs.zfs.io" ]; then
	ERRORS=`expr ${ZEVENT_VDEV_READ_ERRORS} + ${ZEVENT_VDEV_WRITE_ERRORS}`

	if [ ${ZED_SPARE_ON_IO_ERRORS} -gt 0 -a \
	     ${ERRORS} -ge ${ZED_SPARE_ON_IO_ERRORS} ]; then
		ACTION="fault"
	fi
# Degrade devices after N checksum errors.
elif [ "${ZEVENT_CLASS}" = "ereport.fs.zfs.checksum" ]; then
	ERRORS=${ZEVENT_VDEV_CKSUM_ERRORS}

	if [ ${ZED_SPARE_ON_CHECKSUM_ERRORS} -gt 0 -a \
	     ${ERRORS} -ge ${ZED_SPARE_ON_CHECKSUM_ERRORS} ]; then
		ACTION="degrade"
	fi
else
	ACTION=
fi

if [ -n "${ACTION}" ]; then

	# Device is already FAULTED or DEGRADED
	set -- `vdev_status ${ZEVENT_POOL} ${ZEVENT_VDEV_PATH}`
	ZEVENT_VDEV_PATH_FOUND=$1
	STATUS=$2
	if [ "${STATUS}" = "FAULTED" -o "${STATUS}" = "DEGRADED" ]; then
		exit 3
	fi

	# Step 1) FAULT or DEGRADE the device
	#
	${ZINJECT} -d ${ZEVENT_VDEV_GUID} -A ${ACTION} ${ZEVENT_POOL}

	# Step 2) Set the SES fault beacon.
	#
	# XXX: Set the 'fault' or 'ident' beacon for the device.  This can
	# be done through the sg_ses utility, the only hard part is to map
	# the sd device to its corresponding enclosure and slot.  We may
	# be able to leverage the existing vdev_id scripts for this.
	#
	# $ sg_ses --dev-slot-num=0 --set=ident /dev/sg3
	# $ sg_ses --dev-slot-num=0 --clear=ident /dev/sg3

	# Step 3) Replace the device with a hot spare.
	#
	# Round robin through the spares selecting those which are available.
	#
	for SPARE in ${ZEVENT_VDEV_SPARE_PATHS}; do
		set -- `vdev_status ${ZEVENT_POOL} ${SPARE}`
		SPARE_VDEV_FOUND=$1
		STATUS=$2
		if [ "${STATUS}" = "AVAIL" ]; then
			${ZPOOL} replace ${ZEVENT_POOL} \
			    ${ZEVENT_VDEV_GUID} ${SPARE_VDEV_FOUND} && exit 0
		fi
	done

	exit 1
fi

exit 4
