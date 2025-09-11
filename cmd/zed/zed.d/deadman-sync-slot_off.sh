#!/bin/sh
# shellcheck disable=SC3014,SC2154,SC2086,SC2034
#
# Turn off disk's enclosure slot if an I/O is hung triggering the deadman.
#
# It's possible for outstanding I/O to a misbehaving SCSI disk to neither
# promptly complete or return an error.  This can occur due to retry and
# recovery actions taken by the SCSI layer, driver, or disk.  When it occurs
# the pool will be unresponsive even though there may be sufficient redundancy
# configured to proceeded without this single disk.
#
# When a hung I/O is detected by the kmods it will be posted as a deadman
# event.  By default an I/O is considered to be hung after 5 minutes.  This
# value can be changed with the zfs_deadman_ziotime_ms module parameter.
# If ZED_POWER_OFF_ENCLOSURE_SLOT_ON_DEADMAN is set the disk's enclosure
# slot will be powered off causing the outstanding I/O to fail.  The ZED
# will then handle this like a normal disk failure and FAULT the vdev.
#
# We assume the user will be responsible for turning the slot back on
# after replacing the disk.
#
# Note that this script requires that your enclosure be supported by the
# Linux SCSI Enclosure services (SES) driver.  The script will do nothing
# if you have no enclosure, or if your enclosure isn't supported.
#
# Exit codes:
#   0: slot successfully powered off
#   1: enclosure not available
#   2: ZED_POWER_OFF_ENCLOSURE_SLOT_ON_DEADMAN disabled
#   3: System not configured to wait on deadman
#   4: The enclosure sysfs path passed from ZFS does not exist
#   5: Enclosure slot didn't actually turn off after we told it to

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

if [ ! -d /sys/class/enclosure ] ; then
	# No JBOD enclosure or NVMe slots
	exit 1
fi

if [ "${ZED_POWER_OFF_ENCLOSURE_SLOT_ON_DEADMAN}" != "1" ] ; then
	exit 2
fi

if [ "$ZEVENT_POOL_FAILMODE" != "wait" ] ; then
	exit 3
fi

if [ ! -f "$ZEVENT_VDEV_ENC_SYSFS_PATH/power_status" ] ; then
	exit 4
fi

# Turn off the slot and wait for sysfs to report that the slot is off.
# It can take ~400ms on some enclosures and multiple retries may be needed.
for i in $(seq 1 20) ; do
	echo "off" | tee "$ZEVENT_VDEV_ENC_SYSFS_PATH/power_status"

	for j in $(seq 1 5) ; do
		if [ "$(cat $ZEVENT_VDEV_ENC_SYSFS_PATH/power_status)" == "off" ] ; then
			break 2
		fi
		sleep 0.1
	done
done

if [ "$(cat $ZEVENT_VDEV_ENC_SYSFS_PATH/power_status)" != "off" ] ; then
	exit 5
fi

zed_log_msg "powered down slot $ZEVENT_VDEV_ENC_SYSFS_PATH for $ZEVENT_VDEV_PATH"
