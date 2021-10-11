#!/bin/sh
#
# Turn off/on vdevs' enclosure LEDs when their pool's state changes.
# Honors the zed.rc configuration variables ZED_USE_ENCLOSURE_FAULT_LEDS
# and ZED_USE_ENCLOSURE_LOCATE_LEDS.
#
# If both LEDs are available, they are lit as below depending on the vdev
# state. If only one LED is available, it is used to indicate all states below.
#
# State                         Fault LED       Locate LED
# ONLINE                        OFF             OFF
# OFFLINE                       OFF             ON
# FAULTED or DEGRADED           ON              OFF
# REMOVED or UNAVAIL            ON              ON
#
#
# This script run in two basic modes:
#
# 1. If $ZEVENT_VDEV_ENC_SYSFS_PATH and $ZEVENT_VDEV_STATE_STR are set, then
# only set the LED for that particular vdev. This is the case for statechange
# events and some vdev_* events.
#
# 2. If those vars are not set, then check the state of all vdevs in the pool
# and set the LEDs accordingly.  This is the case for pool_import events.
#
# Note that this script requires that your enclosure be supported by the
# Linux SCSI Enclosure services (SES) driver.  The script will do nothing
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

if [ "${ZED_USE_ENCLOSURE_FAULT_LEDS}" != "1" ] && \
	[ "${ZED_USE_ENCLOSURE_LOCATE_LEDS}" != "1" ] && \
	[ "${ZED_USE_ENCLOSURE_LEDS}" != "1" ]
then
	exit 2
fi

# Honor deprecated setting unless newer setting exists.
if [ -z "${ZED_USE_ENCLOSURE_FAULT_LEDS}" ] && [ -n "${ZED_USE_ENCLOSURE_LEDS}" ]
then
	ZED_USE_ENCLOSURE_FAULT_LEDS="${ZED_USE_ENCLOSURE_LEDS}"
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

	if [ -z "$val" ]; then
		return 0
	fi

	if [ ! -e "$file" ] ; then
		return 3
	fi

	# If another process is accessing the LED when we attempt to update it,
	# the update will be lost so retry until the LED actually changes or we
	# timeout.
	for _ in 1 2 3 4 5; do
		# We want to check the current state first, since writing to the
		# led entry always causes a SES command, even if the
		# current state is already what you want.
		read -r current < "${file}"

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
	led="$1"
	state="$2"

	if [ "$led" = "fault" ] && [ "${ZED_USE_ENCLOSURE_FAULT_LEDS}" != "1" ]
	then
		return 0
	fi
	if [ "$led" = "locate" ] && [ "${ZED_USE_ENCLOSURE_LOCATE_LEDS}" != "1" ]
	then
		return 0
	fi

	fault=""
	locate=""

	# Enumerate based on LED states.
	case "$state" in
		ONLINE)
			fault=0
			locate=0
			;;
		OFFLINE)
			fault=0
			locate=1
			;;
		FAULTED|DEGRADED)
			fault=1
			locate=0
			;;
		REMOVED|UNAVAIL)
			fault=1
			locate=1
			;;
		*)
			zed_log_msg "$0 state_to_val state='$state': LED mapping unknown"
			;;
	esac

	# If we're not allowed to use both LEDs, map all states onto the one
	# LED left.
	if [ "${ZED_USE_ENCLOSURE_FAULT_LEDS}" != "1" ] && [ "$fault" = "1" ]; then
		locate=1
	fi
	if [ "${ZED_USE_ENCLOSURE_LOCATE_LEDS}" != "1" ] && [ "$locate" = "1" ]; then
		fault=1
	fi

	case "$led" in
		fault)
			echo "$fault"
			;;
		locate)
			echo "$locate"
			;;
		*)
			zed_log_msg "$0 state_to_val error: led=$led not understood"
			;;
	esac
}

# process_pool (pool)
#
# Iterate through a pool and set the vdevs' enclosure slot LEDs to
# those vdevs' state.
#
# Arguments
#   pool:	Pool name.
#
# Return
#  0 on success, 3 on missing sysfs path
#
process_pool()
{
	pool="$1"

	# The output will be the vdevs only (filtering on '/dev/'):
	#
	#    U45     ONLINE       0     0     0   /dev/sdk
	#    U46     ONLINE       0     0     0   /dev/sdm
	#    U47     ONLINE       0     0     0   /dev/sdn
	#    U50     ONLINE       0     0     0   Potential info message /dev/sdbn
	#
	ZPOOL_SCRIPTS_AS_ROOT=1 $ZPOOL status -c upath "$pool" | awk '/\/dev\// {print $1 " " $2 " " $NF}' | (
	rc=0
	while read -r vdev state dev; do
		# Read out current LED values and path
		# Get dev name (like 'sda')
		dev=$(basename "$dev")
		vdev_enc_sysfs_path=$(realpath "/sys/class/block/$dev/device/enclosure_device"* 2>/dev/null)
		if [ -z "$vdev_enc_sysfs_path" ] ; then
			# Skip anything with no sysfs LED entries
			continue
		fi

		for encled in fault locate; do
			if [ ! -e "$vdev_enc_sysfs_path/$encled" ] ; then
				rc=3
				zed_log_msg "vdev $vdev '$file/$encled' doesn't exist"
				continue
			fi

			val=$(state_to_val "$encled" "$state")

			if ! check_and_set_led "$vdev_enc_sysfs_path/$encled" "$val"; then
				rc=3
			fi
		done
	done
	exit "$rc"; )
}

if [ -n "$ZEVENT_VDEV_ENC_SYSFS_PATH" ] && [ -n "$ZEVENT_VDEV_STATE_STR" ] ; then
	# Got a statechange for an individual vdev
	vdev=$(basename "$ZEVENT_VDEV_PATH")
	for encled in fault locate; do
		val=$(state_to_val "$encled" "$ZEVENT_VDEV_STATE_STR")
		check_and_set_led "$ZEVENT_VDEV_ENC_SYSFS_PATH/$encled" "$val"
	done
else
	# Process the entire pool
	poolname=$(zed_guid_to_pool "$ZEVENT_POOL_GUID")
	process_pool "$poolname"
fi
