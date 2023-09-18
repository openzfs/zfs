#!/bin/sh
# shellcheck disable=SC2154
#
# Turn off/on vdevs' enclosure fault LEDs when their pool's state changes.
#
# Turn a vdev's fault LED on if it becomes FAULTED, DEGRADED or UNAVAIL.
# Turn its LED off when it's back ONLINE again.
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

if [ ! -d /sys/class/enclosure ] && [ ! -d /sys/bus/pci/slots ] ; then
	# No JBOD enclosure or NVMe slots
	exit 1
fi

if [ "${ZED_USE_ENCLOSURE_LEDS}" != "1" ] ; then
	exit 2
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
		# 'fault' entry always causes a SES command, even if the
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

# Fault LEDs for JBODs and NVMe drives are handled a little differently.
#
# On JBODs the fault LED is called 'fault' and on a path like this:
#
#   /sys/class/enclosure/0:0:1:0/SLOT 10/fault
#
# On NVMe it's called 'attention' and on a path like this:
#
#   /sys/bus/pci/slot/0/attention
#
# This function returns the full path to the fault LED file for a given
# enclosure/slot directory.
#
path_to_led()
{
	dir=$1
	if [ -f "$dir/fault" ] ; then
		echo "$dir/fault"
	elif [ -f "$dir/attention" ] ; then
		echo "$dir/attention"
	fi
}

state_to_val()
{
	state="$1"
	case "$state" in
		FAULTED|DEGRADED|UNAVAIL|REMOVED)
			echo 1
			;;
		ONLINE)
			echo 0
			;;
		*)
			echo "invalid state: $state"
			;;
	esac
}

#
# Given a nvme name like 'nvme0n1', pass back its slot directory
# like "/sys/bus/pci/slots/0"
#
nvme_dev_to_slot()
{
	dev="$1"

	# Get the address "0000:01:00.0"
	read -r address < "/sys/class/block/$dev/device/address"

	find /sys/bus/pci/slots -regex '.*/[0-9]+/address$' | \
		while read -r sys_addr; do
			read -r this_address < "$sys_addr"

			# The format of address is a little different between
			# /sys/class/block/$dev/device/address and
			# /sys/bus/pci/slots/
			#
			# address=           "0000:01:00.0"
			# this_address =     "0000:01:00"
			#
			if echo "$address" | grep -Eq ^"$this_address" ; then
				echo "${sys_addr%/*}"
				break
			fi
			done
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

	# The output will be the vdevs only (from "grep '/dev/'"):
	#
	#    U45     ONLINE       0     0     0   /dev/sdk          0
	#    U46     ONLINE       0     0     0   /dev/sdm          0
	#    U47     ONLINE       0     0     0   /dev/sdn          0
	#    U50     ONLINE       0     0     0  /dev/sdbn          0
	#
	ZPOOL_SCRIPTS_AS_ROOT=1 $ZPOOL status -c upath,fault_led "$pool" | grep '/dev/' | (
	rc=0
	while read -r vdev state _ _ _ therest; do
		# Read out current LED value and path
		# Get dev name (like 'sda')
		dev=$(basename "$(echo "$therest" | awk '{print $(NF-1)}')")
		vdev_enc_sysfs_path=$(realpath "/sys/class/block/$dev/device/enclosure_device"*)
		if [ ! -d "$vdev_enc_sysfs_path" ] ; then
			# This is not a JBOD disk, but it could be a PCI NVMe drive
			vdev_enc_sysfs_path=$(nvme_dev_to_slot "$dev")
		fi

		current_val=$(echo "$therest" | awk '{print $NF}')

		if [ "$current_val" != "0" ] ; then
			current_val=1
		fi

		if [ -z "$vdev_enc_sysfs_path" ] ; then
			# Skip anything with no sysfs LED entries
			continue
		fi

		led_path=$(path_to_led "$vdev_enc_sysfs_path")
		if [ ! -e "$led_path" ] ; then
			rc=3
			zed_log_msg "vdev $vdev '$led_path' doesn't exist"
			continue
		fi

		val=$(state_to_val "$state")

		if [ "$current_val" = "$val" ] ; then
			# LED is already set correctly
			continue
		fi

		if ! check_and_set_led "$led_path" "$val"; then
			rc=3
		fi
	done
	exit "$rc"; )
}

if [ -n "$ZEVENT_VDEV_ENC_SYSFS_PATH" ] && [ -n "$ZEVENT_VDEV_STATE_STR" ] ; then
	# Got a statechange for an individual vdev
	val=$(state_to_val "$ZEVENT_VDEV_STATE_STR")
	vdev=$(basename "$ZEVENT_VDEV_PATH")
	ledpath=$(path_to_led "$ZEVENT_VDEV_ENC_SYSFS_PATH")
	check_and_set_led "$ledpath" "$val"
else
	# Process the entire pool
	poolname=$(zed_guid_to_pool "$ZEVENT_POOL_GUID")
	process_pool "$poolname"
fi
