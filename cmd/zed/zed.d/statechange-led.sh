#!/bin/sh
#
# Turn off/on the VDEV's enclosure fault LEDs when the pool's state changes.
#
# Turn the VDEV's fault LED on if it becomes FAULTED, DEGRADED or UNAVAIL.
# Turn the LED off when it's back ONLINE again.
#
# This script run in two basic modes:
#
# 1. If $ZEVENT_VDEV_ENC_SYSFS_PATH and $ZEVENT_VDEV_STATE_STR are set, then
# only set the LED for that particular VDEV. This is the case for statechange
# events and some vdev_* events.
#
# 2. If those vars are not set, then check the state of all VDEVs in the pool
# and set the LEDs accordingly.  This is the case for pool_import events.
#
# Note that this script requires that your enclosure be supported by the
# Linux SCSI enclosure services (ses) driver.  The script will do nothing
# if you have no enclosure, or if your enclosure isn't supported.
#
# Exit codes:
#   0: enclosure led successfully set
#   1: enclosure leds not available
#   2: enclosure leds administratively disabled
#   3: The led sysfs path passed from ZFS does not exist
#   4: $ZPOOL not set
#   5: awk is not installed

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

if [ ! -d /sys/class/enclosure ] ; then
	exit 1
fi

if [ "${ZED_USE_ENCLOSURE_LEDS}" != "1" ] ; then
	exit 2
fi

if [ -z "$ZEVENT_VDEV_ENC_SYSFS_PATH" ] || [ -z "$ZEVENT_VDEV_STATE_STR" ] ; then
	# Something odd is going on, these vars should be set by ZED
	exit 3
fi

zed_check_cmd "$ZPOOL" || exit 4
zed_check_cmd awk || exit 5

# Global used in set_led debug print
vdev=""

# check_and_set_led (file, val)
#
# Read an enclosure sysfs file, and write it if it's not already set to 'val'
#
# Arguments
#   file: sysfs file to set (like /sys/class/enclosure/0:0:1:0/SLOT 10/fault)
#   val: value to set it to
#
# Return
#  0 on success, 3 on missing sysfs path
#
check_and_set_led()
{
	file="$1"
	val="$2"

	if [ ! -e "$file" ] ; then
		return 3
	fi

	# If another process is accessing the LED when we attempt to update it,
	# the update will be lost so retry until the LED actually changes or we
	# timeout.
	for _ in $(seq 1 5); do
		# We want to check the current state first, since writing to the
		# 'fault' entry always causes a SES command, even if the
		# current state is already what you want.
		current=$(cat "${file}")

		# On some enclosures if you write 1 to fault, and read it back,
		# it will return 2.  Treat all non-zero values as 1 for
		# simplicity.
		if [ "$current" != "0" ] ; then
			current=1
		fi

		if [ "$current" != "$val" ] ; then
			echo "$val" > "$file"
			zed_log_msg "vdev $vdev set '$file' LED to $val"
		else
			break
		fi
        done
}

state_to_val()
{
	state="$1"
	if [ "$state" = "FAULTED" ] || [ "$state" = "DEGRADED" ] || \
	   [ "$state" = "UNAVAIL" ] ; then
		echo 1
	elif [ "$state" = "ONLINE" ] ; then
		echo 0
	fi
}

# Got a statechange for an individual VDEV
val=$(state_to_val "$ZEVENT_VDEV_STATE_STR")
vdev=$(basename "$ZEVENT_VDEV_PATH")
check_and_set_led "$ZEVENT_VDEV_ENC_SYSFS_PATH/fault" "$val"
