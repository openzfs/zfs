#!/bin/bash
#
# Turn off/on the VDEV's enclosure fault LEDs when the pool's state changes.
#
# Turn LED on if the VDEV becomes faulted or degraded, and turn it back off
# when it's online again.  It will also turn on the LED (or keep it on) if
# the drive becomes unavailable, unless the drive was in was a previously
# online state (online->unavail is a normal state transition during an
# autoreplace).
#
# This script requires that your enclosure be supported by the
# Linux SCSI enclosure services (ses) driver.  The script will do nothing
# if you have no enclosure, or if your enclosure isn't supported.
#
# This script also requires ZFS to be built with libdevmapper support.
#
# Exit codes:
#   0: enclosure led successfully set
#   1: enclosure leds not not available
#   2: enclosure leds administratively disabled
#   3: ZED didn't pass enclosure sysfs path
#   4: Enclosure sysfs path doesn't exist

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

if [ ! -d /sys/class/enclosure ] ; then
	exit 1
fi

if [ "${ZED_USE_ENCLOSURE_LEDS}" != "1" ] ; then
	exit 2
fi

[ -n "${ZEVENT_VDEV_ENC_SYSFS_PATH}" ] || exit 3

[ -e "${ZEVENT_VDEV_ENC_SYSFS_PATH}/fault" ] || exit 4

# Turn on/off enclosure LEDs
function led
{
	file="$1/fault"
	val=$2

	# We want to check the current state first, since writing to the
	# 'fault' entry always always causes a SES command, even if the
	# current state is already what you want.
	current=$(cat "${file}")

	# On some enclosures if you write 1 to fault, and read it back,
	# it will return 2.  Treat all non-zero values as 1 for
	# simplicity.
	if [ "$current" != "0" ] ; then
		current=1
	fi

	if [ "$current" != "$val" ] ; then
		# Set the value twice.  I've seen enclosures that were
		# flakey about setting it the first time.
		echo "$val" > "$file"
		echo "$val" > "$file"
	fi
}

# Decide whether to turn on/off an LED based on the state
# Pass in path name and fault string ("ONLINE"/"FAULTED"/"DEGRADED"...etc)
#
# We only turn on LEDs when a drive becomes FAULTED, DEGRADED, or UNAVAIL and
# only turn it on when it comes back ONLINE.  All other states are ignored, and
# keep the previous LED state.  
function process {
	path="$1"
	fault=$2
	if [ "$fault" == "FAULTED" ] || [ "$fault" == "DEGRADED" ] || \
	   [ "$fault" == "UNAVAIL" ] ; then
		led "$path" 1
	elif [ "$fault" == "ONLINE" ] ; then
		led "$path" 0
	fi
}

process "$ZEVENT_VDEV_ENC_SYSFS_PATH" "$ZEVENT_VDEV_STATE_STR"
