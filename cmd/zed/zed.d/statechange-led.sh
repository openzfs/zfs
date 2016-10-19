#!/bin/bash
#
# Turn off/on the VDEV's enclosure fault LEDs when the pool's state changes.
#
# Turn LED on if the VDEV becomes faulted/degraded, and turn it back off when
# it's healthy again.  This requires that your enclosure be supported by the
# Linux SCSI enclosure services (ses) driver.  The script will do nothing
# if you have no enclosure, or if your enclosure isn't supported.
#
# This script also requires ZFS to be built with libdevmapper support.
#
# Exit codes:
#   0: enclosure led successfully set
#   1: enclosure leds not not available
#   2: enclosure leds administratively disabled
#   3: ZED built without libdevmapper

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

# ZEVENT_VDEV_UPATH will not be present if ZFS is not built with libdevmapper
[ -n "${ZEVENT_VDEV_UPATH}" ] || exit 3

if [ "${ZED_USE_ENCLOSURE_LEDS}" != "1" ] ; then
	exit 2
fi

if [ ! -d /sys/class/enclosure ] ; then
	exit 1
fi

# Turn on/off enclosure LEDs
function led
{
	name=$1
	val=$2

	# We want to check the current state first, since writing to the
	# 'fault' entry always always causes a SES command, even if the
	# current state is already what you want.
	if [ -e /sys/block/$name/device/enclosure_device*/fault ] ; then
		# We have to do some monkey business to deal with spaces in
		# enclosure_device names.  I've seen horrible things like this: 
		#
		# '/sys/block/sdfw/device/enclosure_device:SLOT 43 41  /fault'
		#
		# ...so escape all spaces.
		file=`ls /sys/block/$name/device/enclosure_device*/fault | sed 's/\s/\\ /g'`

		current=`cat "$file"`

		# On some enclosures if you write 1 to fault, and read it back,
		# it will return 2.  Treat all non-zero values as 1 for
		# simplicity.
		if [ "$current" != "0" ] ; then
			current=1
		fi

		if [ "$current" != "$val" ] ; then
			# Set the value twice.  I've seen enclosures that were
			# flakey about setting it the first time.
			echo $val > "$file"
			echo $val > "$file"
		fi
	fi
}

# Decide whether to turn on/off an LED based on the state
# Pass in path name and fault string ("ONLINE"/"FAULTED"/"DEGRADED"...etc)
function process {
	# path=/dev/sda, fault=

	path=$1
	fault=$2
	name=`basename $path`

	if [ -z "$name" ] ; then
		return
	fi

	if [ "$fault" == "FAULTED" ] || [ "$fault" == "DEGRADED" ] ; then
		led $name 1
	else
		led $name 0
	fi
}

process "$ZEVENT_VDEV_UPATH" "$ZEVENT_VDEV_STATE_STR"
