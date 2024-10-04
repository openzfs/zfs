#!/bin/sh
#
# vdev_id: udev helper to generate user-friendly names for JBOD disks
#
# This script parses the file /etc/zfs/vdev_id.conf to map a
# physical path in a storage topology to a channel name.  The
# channel name is combined with a disk enclosure slot number to
# create an alias that reflects the physical location of the drive.
# This is particularly helpful when it comes to tasks like replacing
# failed drives.  Slot numbers may also be re-mapped in case the
# default numbering is unsatisfactory.  The drive aliases will be
# created as symbolic links in /dev/disk/by-vdev.
#
# The currently supported topologies are sas_direct and sas_switch.
# A multipath mode is supported in which dm-mpath devices are
# handled by examining the first-listed running component disk.  In
# multipath mode the configuration file should contain a channel
# definition with the same name for each path to a given enclosure.
#
# The alias keyword provides a simple way to map already-existing
# device symlinks to more convenient names.  It is suitable for
# small, static configurations or for sites that have some automated
# way to generate the mapping file.
#
#
# Some example configuration files are given below.

# #
# # Example vdev_id.conf - sas_direct.
# #
#
# multipath     no
# topology      sas_direct
# phys_per_port 4
# slot          bay
#
# #       PCI_ID  HBA PORT  CHANNEL NAME
# channel 85:00.0 1         A
# channel 85:00.0 0         B
# channel 86:00.0 1         C
# channel 86:00.0 0         D
#
# # Custom mapping for Channel A
#
# #    Linux      Mapped
# #    Slot       Slot      Channel
# slot 1          7         A
# slot 2          10        A
# slot 3          3         A
# slot 4          6         A
#
# # Default mapping for B, C, and D
# slot 1          4
# slot 2          2
# slot 3          1
# slot 4          3

# #
# # Example vdev_id.conf - sas_switch
# #
#
# topology      sas_switch
#
# #       SWITCH PORT  CHANNEL NAME
# channel 1            A
# channel 2            B
# channel 3            C
# channel 4            D

# #
# # Example vdev_id.conf - multipath
# #
#
# multipath yes
#
# #       PCI_ID  HBA PORT  CHANNEL NAME
# channel 85:00.0 1         A
# channel 85:00.0 0         B
# channel 86:00.0 1         A
# channel 86:00.0 0         B

# #
# # Example vdev_id.conf - multipath / multijbod-daisychaining
# #
#
# multipath yes
# multijbod yes
#
# #       PCI_ID  HBA PORT  CHANNEL NAME
# channel 85:00.0 1         A
# channel 85:00.0 0         B
# channel 86:00.0 1         A
# channel 86:00.0 0         B

# #
# # Example vdev_id.conf - multipath / mixed
# #
#
# multipath yes
# slot mix
#
# #       PCI_ID  HBA PORT  CHANNEL NAME
# channel 85:00.0 3         A
# channel 85:00.0 2         B
# channel 86:00.0 3         A
# channel 86:00.0 2         B
# channel af:00.0 0         C
# channel af:00.0 1         C

# #
# # Example vdev_id.conf - alias
# #
#
# #     by-vdev
# #     name     fully qualified or base name of device link
# alias d1       /dev/disk/by-id/wwn-0x5000c5002de3b9ca
# alias d2       wwn-0x5000c5002def789e

PATH=/bin:/sbin:/usr/bin:/usr/sbin
CONFIG=/etc/zfs/vdev_id.conf
PHYS_PER_PORT=
DEV=
TOPOLOGY=
BAY=
ENCL_ID=""
UNIQ_ENCL_ID=""
ZPAD=1

usage() {
	cat << EOF
Usage: vdev_id [-h]
       vdev_id <-d device> [-c config_file] [-p phys_per_port]
               [-g sas_direct|sas_switch|scsi] [-m]

  -c    specify name of an alternative config file [default=$CONFIG]
  -d    specify basename of device (i.e. sda)
  -e    Create enclose device symlinks only (/dev/by-enclosure)
  -g    Storage network topology [default="$TOPOLOGY"]
  -m    Run in multipath mode
  -j    Run in multijbod mode
  -p    number of phy's per switch port [default=$PHYS_PER_PORT]
  -h    show this summary
EOF
	exit 1
	# exit with error to avoid processing usage message by a udev rule
}

map_slot() {
	LINUX_SLOT=$1
	CHANNEL=$2

	MAPPED_SLOT=$(awk -v linux_slot="$LINUX_SLOT" -v channel="$CHANNEL" \
			'$1 == "slot" && $2 == linux_slot && \
			($4 ~ "^"channel"$" || $4 ~ /^$/) { print $3; exit}' $CONFIG)
	if [ -z "$MAPPED_SLOT" ] ; then
		MAPPED_SLOT=$LINUX_SLOT
	fi
	printf "%0${ZPAD}d" "${MAPPED_SLOT}"
}

map_channel() {
	MAPPED_CHAN=
	PCI_ID=$1
	PORT=$2

	case $TOPOLOGY in
		"sas_switch")
		MAPPED_CHAN=$(awk -v port="$PORT" \
			'$1 == "channel" && $2 == port \
			{ print $3; exit }' $CONFIG)
		;;
		"sas_direct"|"scsi")
		MAPPED_CHAN=$(awk -v pciID="$PCI_ID" -v port="$PORT" \
			'$1 == "channel" && $2 == pciID && $3 == port \
			{print $4}' $CONFIG)
		;;
	esac
	printf "%s" "${MAPPED_CHAN}"
}

get_encl_id() {
	set -- $(echo $1)
	count=$#

	i=1
	while [ $i -le $count ] ; do
		d=$(eval echo '$'{$i})
		id=$(cat "/sys/class/enclosure/${d}/id")
		ENCL_ID="${ENCL_ID} $id"
		i=$((i + 1))
	done
}

get_uniq_encl_id() {
	for uuid in ${ENCL_ID}; do
		found=0

		for count in ${UNIQ_ENCL_ID}; do
			if [ $count = $uuid ]; then
				found=1
				break
			fi
		done

		if [ $found -eq 0 ]; then
			UNIQ_ENCL_ID="${UNIQ_ENCL_ID} $uuid"
		fi
	done
}

# map_jbod explainer: The bsg driver knows the difference between a SAS
# expander and fanout expander. Use hostX instance along with top-level
# (whole enclosure) expander instances in /sys/class/enclosure and
# matching a field in an array of expanders, using the index of the
# matched array field as the enclosure instance, thereby making jbod IDs
# dynamic. Avoids reliance on high overhead userspace commands like
# multipath and lsscsi and instead uses existing sysfs data.  $HOSTCHAN
# variable derived from devpath gymnastics in sas_handler() function.
map_jbod() {
	DEVEXP=$(ls -l "/sys/block/$DEV/device/" | grep enclos | awk -F/ '{print $(NF-1) }')
	DEV=$1

	# Use "set --" to create index values (Arrays)
	set -- $(ls -l /sys/class/enclosure | grep -v "^total" | awk '{print $9}')
	# Get count of total elements
	JBOD_COUNT=$#
	JBOD_ITEM=$*

	# Build JBODs (enclosure)  id from sys/class/enclosure/<dev>/id
	get_encl_id "$JBOD_ITEM"
	# Different expander instances for each paths.
	# Filter out and keep only unique id.
	get_uniq_encl_id

	# Identify final 'mapped jbod'
	j=0
	for count in ${UNIQ_ENCL_ID}; do
		i=1
		j=$((j + 1))
		while [ $i -le $JBOD_COUNT ] ; do
			d=$(eval echo '$'{$i})
			id=$(cat "/sys/class/enclosure/${d}/id")
			if [ "$d" = "$DEVEXP" ] && [ $id = $count ] ; then
				MAPPED_JBOD=$j
				break
			fi
			i=$((i + 1))
		done
	done

	printf "%d" "${MAPPED_JBOD}"
}

sas_handler() {
	if [ -z "$PHYS_PER_PORT" ] ; then
		PHYS_PER_PORT=$(awk '$1 == "phys_per_port" \
			{print $2; exit}' $CONFIG)
	fi
	PHYS_PER_PORT=${PHYS_PER_PORT:-4}

	if ! echo "$PHYS_PER_PORT" | grep -q -E '^[0-9]+$' ; then
		echo "Error: phys_per_port value $PHYS_PER_PORT is non-numeric"
		exit 1
	fi

	if [ -z "$MULTIPATH_MODE" ] ; then
		MULTIPATH_MODE=$(awk '$1 == "multipath" \
			{print $2; exit}' $CONFIG)
	fi

	if [ -z "$MULTIJBOD_MODE" ] ; then
		MULTIJBOD_MODE=$(awk '$1 == "multijbod" \
			{print $2; exit}' $CONFIG)
	fi

	# Use first running component device if we're handling a dm-mpath device
	if [ "$MULTIPATH_MODE" = "yes" ] ; then
		# If udev didn't tell us the UUID via DM_NAME, check /dev/mapper
		if [ -z "$DM_NAME" ] ; then
			DM_NAME=$(ls -l --full-time /dev/mapper |
				grep "$DEV"$ | awk '{print $9}')
		fi

		# For raw disks udev exports DEVTYPE=partition when
		# handling partitions, and the rules can be written to
		# take advantage of this to append a -part suffix.  For
		# dm devices we get DEVTYPE=disk even for partitions so
		# we have to append the -part suffix directly in the
		# helper.
		if [ "$DEVTYPE" != "partition" ] ; then
			# Match p[number], remove the 'p' and prepend "-part"
			PART=$(echo "$DM_NAME" |
				awk 'match($0,/p[0-9]+$/) {print "-part"substr($0,RSTART+1,RLENGTH-1)}')
		fi

		# Strip off partition information.
		DM_NAME=$(echo "$DM_NAME" | sed 's/p[0-9][0-9]*$//')
		if [ -z "$DM_NAME" ] ; then
			return
		fi

		# Utilize DM device name to gather subordinate block devices
		# using sysfs to avoid userspace utilities

		# If our DEVNAME is something like /dev/dm-177, then we may be
		# able to get our DMDEV from it.
		DMDEV=$(echo $DEVNAME | sed 's;/dev/;;g')
		if [ ! -e /sys/block/$DMDEV/slaves/* ] ; then
			# It's not there, try looking in /dev/mapper
			DMDEV=$(ls -l --full-time /dev/mapper | grep $DM_NAME |
			awk '{gsub("../", " "); print $NF}')
		fi

		# Use sysfs pointers in /sys/block/dm-X/slaves because using
		# userspace tools creates lots of overhead and should be avoided
		# whenever possible. Use awk to isolate lowest instance of
		# sd device member in dm device group regardless of string
		# length.
		DEV=$(ls "/sys/block/$DMDEV/slaves" | awk '
			{ len=sprintf ("%20s",length($0)); gsub(/ /,0,str); a[NR]=len "_" $0; }
			END {
				asort(a)
				print substr(a[1],22)
			}')

		if [ -z "$DEV" ] ; then
			return
		fi
	fi

	if echo "$DEV" | grep -q ^/devices/ ; then
		sys_path=$DEV
	else
		sys_path=$(udevadm info -q path -p "/sys/block/$DEV" 2>/dev/null)
	fi

	# Use positional parameters as an ad-hoc array
	set -- $(echo "$sys_path" | tr / ' ')
	num_dirs=$#
	scsi_host_dir="/sys"

	# Get path up to /sys/.../hostX
	i=1

	while [ $i -le "$num_dirs" ] ; do
		d=$(eval echo '$'{$i})
		scsi_host_dir="$scsi_host_dir/$d"
		echo "$d" | grep -q -E '^host[0-9]+$' && break
		i=$((i + 1))
	done

	# Lets grab the SAS host channel number and save it for JBOD sorting later
	HOSTCHAN=$(echo "$d" | awk -F/ '{ gsub("host","",$NF); print $NF}')

	if [ $i = "$num_dirs" ] ; then
		return
	fi

	PCI_ID=$(eval echo '$'{$((i -1))} | awk -F: '{print $2":"$3}')

	# In sas_switch mode, the directory four levels beneath
	# /sys/.../hostX contains symlinks to phy devices that reveal
	# the switch port number.  In sas_direct mode, the phy links one
	# directory down reveal the HBA port.
	port_dir=$scsi_host_dir

	case $TOPOLOGY in
		"sas_switch") j=$((i + 4)) ;;
		"sas_direct") j=$((i + 1)) ;;
	esac

	i=$((i + 1))

	while [ $i -le $j ] ; do
		port_dir="$port_dir/$(eval echo '$'{$i})"
		i=$((i + 1))
	done

	PHY=$(ls -vd "$port_dir"/phy* 2>/dev/null | head -1 | awk -F: '{print $NF}')
	if [ -z "$PHY" ] ; then
		PHY=0
	fi
	PORT=$((PHY / PHYS_PER_PORT))

	# Look in /sys/.../sas_device/end_device-X for the bay_identifier
	# attribute.
	end_device_dir=$port_dir

	while [ $i -lt "$num_dirs" ] ; do
		d=$(eval echo '$'{$i})
		end_device_dir="$end_device_dir/$d"
		if echo "$d" | grep -q '^end_device' ; then
			end_device_dir="$end_device_dir/sas_device/$d"
			break
		fi
		i=$((i + 1))
	done

	# Add 'mix' slot type for environments where dm-multipath devices
	# include end-devices connected via SAS expanders or direct connection
	# to SAS HBA. A mixed connectivity environment such as pool devices
	# contained in a SAS JBOD and spare drives or log devices directly
	# connected in a server backplane without expanders in the I/O path.
	SLOT=

	case $BAY in
	"bay")
		SLOT=$(cat "$end_device_dir/bay_identifier" 2>/dev/null)
		;;
	"mix")
		if [ $(cat "$end_device_dir/bay_identifier" 2>/dev/null) ] ; then
			SLOT=$(cat "$end_device_dir/bay_identifier" 2>/dev/null)
		else
			SLOT=$(cat "$end_device_dir/phy_identifier" 2>/dev/null)
		fi
		;;
	"phy")
		SLOT=$(cat "$end_device_dir/phy_identifier" 2>/dev/null)
		;;
	"port")
		d=$(eval echo '$'{$i})
		SLOT=$(echo "$d" | sed -e 's/^.*://')
		;;
	"id")
		i=$((i + 1))
		d=$(eval echo '$'{$i})
		SLOT=$(echo "$d" | sed -e 's/^.*://')
		;;
	"lun")
		i=$((i + 2))
		d=$(eval echo '$'{$i})
		SLOT=$(echo "$d" | sed -e 's/^.*://')
		;;
	"bay_lun")
		# Like 'bay' but with the LUN number appened. Added for SAS 
		# multi-actuator HDDs, where one physical drive has multiple
		# LUNs, thus multiple logical drives share the same bay number
		i=$((i + 2))
		d=$(eval echo '$'{$i})
		LUN="-lun$(echo "$d" | sed -e 's/^.*://')"
		SLOT=$(cat "$end_device_dir/bay_identifier" 2>/dev/null)
		;;
	"ses")
		# look for this SAS path in all SCSI Enclosure Services
		# (SES) enclosures
		sas_address=$(cat "$end_device_dir/sas_address" 2>/dev/null)
		enclosures=$(lsscsi -g | \
			sed -n -e '/enclosu/s/^.* \([^ ][^ ]*\) *$/\1/p')
		for enclosure in $enclosures; do
			set -- $(sg_ses -p aes "$enclosure" | \
				awk "/device slot number:/{slot=\$12} \
					/SAS address: $sas_address/\
					{print slot}")
			SLOT=$1
			if [ -n "$SLOT" ] ; then
				break
			fi
		done
		;;
	esac
	if [ -z "$SLOT" ] ; then
		return
	fi

	if [ "$MULTIJBOD_MODE" = "yes" ] ; then
		CHAN=$(map_channel "$PCI_ID" "$PORT")
		SLOT=$(map_slot "$SLOT" "$CHAN")
		JBOD=$(map_jbod "$DEV")

		if [ -z "$CHAN" ] ; then
			return
		fi
		echo "${CHAN}"-"${JBOD}"-"${SLOT}${LUN}${PART}"
	else
		CHAN=$(map_channel "$PCI_ID" "$PORT")
		SLOT=$(map_slot "$SLOT" "$CHAN")

		if [ -z "$CHAN" ] ; then
			return
		fi
		echo "${CHAN}${SLOT}${LUN}${PART}"
	fi
}

scsi_handler() {
	if [ -z "$FIRST_BAY_NUMBER" ] ; then
		FIRST_BAY_NUMBER=$(awk '$1 == "first_bay_number" \
			{print $2; exit}' $CONFIG)
	fi
	FIRST_BAY_NUMBER=${FIRST_BAY_NUMBER:-0}

	if [ -z "$PHYS_PER_PORT" ] ; then
		PHYS_PER_PORT=$(awk '$1 == "phys_per_port" \
			{print $2; exit}' $CONFIG)
	fi
	PHYS_PER_PORT=${PHYS_PER_PORT:-4}

	if ! echo "$PHYS_PER_PORT" | grep -q -E '^[0-9]+$' ; then
		echo "Error: phys_per_port value $PHYS_PER_PORT is non-numeric"
		exit 1
	fi

	if [ -z "$MULTIPATH_MODE" ] ; then
		MULTIPATH_MODE=$(awk '$1 == "multipath" \
			{print $2; exit}' $CONFIG)
	fi

	# Use first running component device if we're handling a dm-mpath device
	if [ "$MULTIPATH_MODE" = "yes" ] ; then
		# If udev didn't tell us the UUID via DM_NAME, check /dev/mapper
		if [ -z "$DM_NAME" ] ; then
			DM_NAME=$(ls -l --full-time /dev/mapper |
				grep "$DEV"$ | awk '{print $9}')
		fi

		# For raw disks udev exports DEVTYPE=partition when
		# handling partitions, and the rules can be written to
		# take advantage of this to append a -part suffix.  For
		# dm devices we get DEVTYPE=disk even for partitions so
		# we have to append the -part suffix directly in the
		# helper.
		if [ "$DEVTYPE" != "partition" ] ; then
			# Match p[number], remove the 'p' and prepend "-part"
			PART=$(echo "$DM_NAME" |
			    awk 'match($0,/p[0-9]+$/) {print "-part"substr($0,RSTART+1,RLENGTH-1)}')
		fi

		# Strip off partition information.
		DM_NAME=$(echo "$DM_NAME" | sed 's/p[0-9][0-9]*$//')
		if [ -z "$DM_NAME" ] ; then
			return
		fi

		# Get the raw scsi device name from multipath -ll. Strip off
		# leading pipe symbols to make field numbering consistent.
		DEV=$(multipath -ll "$DM_NAME" |
			awk '/running/{gsub("^[|]"," "); print $3 ; exit}')
		if [ -z "$DEV" ] ; then
			return
		fi
	fi

	if echo "$DEV" | grep -q ^/devices/ ; then
		sys_path=$DEV
	else
		sys_path=$(udevadm info -q path -p "/sys/block/$DEV" 2>/dev/null)
	fi

	# expect sys_path like this, for example:
	# /devices/pci0000:00/0000:00:0b.0/0000:09:00.0/0000:0a:05.0/0000:0c:00.0/host3/target3:1:0/3:1:0:21/block/sdv

	# Use positional parameters as an ad-hoc array
	set -- $(echo "$sys_path" | tr / ' ')
	num_dirs=$#
	scsi_host_dir="/sys"

	# Get path up to /sys/.../hostX
	i=1

	while [ $i -le "$num_dirs" ] ; do
		d=$(eval echo '$'{$i})
		scsi_host_dir="$scsi_host_dir/$d"

		echo "$d" | grep -q -E '^host[0-9]+$' && break
		i=$((i + 1))
	done

	if [ $i = "$num_dirs" ] ; then
		return
	fi

	PCI_ID=$(eval echo '$'{$((i -1))} | awk -F: '{print $2":"$3}')

	# In scsi mode, the directory two levels beneath
	# /sys/.../hostX reveals the port and slot.
	port_dir=$scsi_host_dir
	j=$((i + 2))

	i=$((i + 1))
	while [ $i -le $j ] ; do
		port_dir="$port_dir/$(eval echo '$'{$i})"
		i=$((i + 1))
	done

	set -- $(echo "$port_dir" | sed -e 's/^.*:\([^:]*\):\([^:]*\)$/\1 \2/')
	PORT=$1
	SLOT=$(($2 + FIRST_BAY_NUMBER))

	if [ -z "$SLOT" ] ; then
		return
	fi

	CHAN=$(map_channel "$PCI_ID" "$PORT")
	SLOT=$(map_slot "$SLOT" "$CHAN")

	if [ -z "$CHAN" ] ; then
		return
	fi
	echo "${CHAN}${SLOT}${PART}"
}

# Figure out the name for the enclosure symlink
enclosure_handler () {
	# We get all the info we need from udev's DEVPATH variable:
	#
	# DEVPATH=/sys/devices/pci0000:00/0000:00:03.0/0000:05:00.0/host0/subsystem/devices/0:0:0:0/scsi_generic/sg0

	# Get the enclosure ID ("0:0:0:0")
	ENC="${DEVPATH%/*}"
	ENC="${ENC%/*}"
	ENC="${ENC##*/}"
	if [ ! -d "/sys/class/enclosure/$ENC" ] ; then
		# Not an enclosure, bail out
		return
	fi

	# Get the long sysfs device path to our enclosure. Looks like:
	# /devices/pci0000:00/0000:00:03.0/0000:05:00.0/host0/port-0:0/ ... /enclosure/0:0:0:0

	ENC_DEVICE=$(readlink "/sys/class/enclosure/$ENC")

	# Grab the full path to the hosts port dir:
	# /devices/pci0000:00/0000:00:03.0/0000:05:00.0/host0/port-0:0
	PORT_DIR=$(echo "$ENC_DEVICE" | grep -Eo '.+host[0-9]+/port-[0-9]+:[0-9]+')

	# Get the port number
	PORT_ID=$(echo "$PORT_DIR" | grep -Eo "[0-9]+$")

	# The PCI directory is two directories up from the port directory
	# /sys/devices/pci0000:00/0000:00:03.0/0000:05:00.0
	PCI_ID_LONG="$(readlink -m "/sys/$PORT_DIR/../..")"
	PCI_ID_LONG="${PCI_ID_LONG##*/}"

	# Strip down the PCI address from 0000:05:00.0 to 05:00.0
	PCI_ID="${PCI_ID_LONG#[0-9]*:}"

	# Name our device according to vdev_id.conf (like "L0" or "U1").
	NAME=$(awk "/channel/{if (\$1 == \"channel\" && \$2 == \"$PCI_ID\" && \
		\$3 == \"$PORT_ID\") {print \$4\$3}}" $CONFIG)

	echo "${NAME}"
}

alias_handler () {
	# Special handling is needed to correctly append a -part suffix
	# to partitions of device mapper devices.  The DEVTYPE attribute
	# is normally set to "disk" instead of "partition" in this case,
	# so the udev rules won't handle that for us as they do for
	# "plain" block devices.
	#
	# For example, we may have the following links for a device and its
	# partitions,
	#
	#  /dev/disk/by-id/dm-name-isw_dibgbfcije_ARRAY0   -> ../../dm-0
	#  /dev/disk/by-id/dm-name-isw_dibgbfcije_ARRAY0p1 -> ../../dm-1
	#  /dev/disk/by-id/dm-name-isw_dibgbfcije_ARRAY0p2 -> ../../dm-3
	#
	# and the following alias in vdev_id.conf.
	#
	#   alias A0 dm-name-isw_dibgbfcije_ARRAY0
	#
	# The desired outcome is for the following links to be created
	# without having explicitly defined aliases for the partitions.
	#
	#  /dev/disk/by-vdev/A0       -> ../../dm-0
	#  /dev/disk/by-vdev/A0-part1 -> ../../dm-1
	#  /dev/disk/by-vdev/A0-part2 -> ../../dm-3
	#
	# Warning: The following grep pattern will misidentify whole-disk
	#          devices whose names end with 'p' followed by a string of
	#          digits as partitions, causing alias creation to fail. This
	#          ambiguity seems unavoidable, so devices using this facility
	#          must not use such names.
	DM_PART=
	if echo "$DM_NAME" | grep -q -E 'p[0-9][0-9]*$' ; then
		if [ "$DEVTYPE" != "partition" ] ; then
			# Match p[number], remove the 'p' and prepend "-part"
			DM_PART=$(echo "$DM_NAME" |
			    awk 'match($0,/p[0-9]+$/) {print "-part"substr($0,RSTART+1,RLENGTH-1)}')
		fi
	fi

	# DEVLINKS attribute must have been populated by already-run udev rules.
	for link in $DEVLINKS ; do
		# Remove partition information to match key of top-level device.
		if [ -n "$DM_PART" ] ; then
			link=$(echo "$link" | sed 's/p[0-9][0-9]*$//')
		fi
		# Check both the fully qualified and the base name of link.
		for l in $link ${link##*/} ; do
			if [ ! -z "$l" ]; then
				alias=$(awk -v var="$l" '($1 == "alias") && \
					($3 == var) \
					{ print $2; exit }' $CONFIG)
				if [ -n "$alias" ] ; then
					echo "${alias}${DM_PART}"
					return
				fi
			fi
		done
	done
}

# main
while getopts 'c:d:eg:jmp:h' OPTION; do
	case ${OPTION} in
	c)
		CONFIG=${OPTARG}
		;;
	d)
		DEV=${OPTARG}
		;;
	e)
	# When udev sees a scsi_generic device, it calls this script with -e to
	# create the enclosure device symlinks only.  We also need
	# "enclosure_symlinks yes" set in vdev_id.config to actually create the
	# symlink.
	ENCLOSURE_MODE=$(awk '{if ($1 == "enclosure_symlinks") \
		print $2}' "$CONFIG")

	if [ "$ENCLOSURE_MODE" != "yes" ] ; then
		exit 0
	fi
		;;
	g)
		TOPOLOGY=$OPTARG
		;;
	p)
		PHYS_PER_PORT=${OPTARG}
		;;
	j)
		MULTIJBOD_MODE=yes
		;;
	m)
		MULTIPATH_MODE=yes
		;;
	h)
		usage
		;;
	esac
done

if [ ! -r "$CONFIG" ] ; then
	echo "Error: Config file \"$CONFIG\" not found"
	exit 1
fi

if [ -z "$DEV" ] && [ -z "$ENCLOSURE_MODE" ] ; then
	echo "Error: missing required option -d"
	exit 1
fi

if [ -z "$TOPOLOGY" ] ; then
	TOPOLOGY=$(awk '($1 == "topology") {print $2; exit}' "$CONFIG")
fi

if [ -z "$BAY" ] ; then
	BAY=$(awk '($1 == "slot") {print $2; exit}' "$CONFIG")
fi

ZPAD=$(awk '($1 == "zpad_slot") {print $2; exit}' "$CONFIG")

TOPOLOGY=${TOPOLOGY:-sas_direct}

# Should we create /dev/by-enclosure symlinks?
if [ "$ENCLOSURE_MODE" = "yes" ] && [ "$TOPOLOGY" = "sas_direct" ] ; then
	ID_ENCLOSURE=$(enclosure_handler)
	if [ -z "$ID_ENCLOSURE" ] ; then
		exit 0
	fi

	# Just create the symlinks to the enclosure devices and then exit.
	ENCLOSURE_PREFIX=$(awk '/enclosure_symlinks_prefix/{print $2}' "$CONFIG")
	if [ -z "$ENCLOSURE_PREFIX" ] ; then
		ENCLOSURE_PREFIX="enc"
	fi
	echo "ID_ENCLOSURE=$ID_ENCLOSURE"
	echo "ID_ENCLOSURE_PATH=by-enclosure/$ENCLOSURE_PREFIX-$ID_ENCLOSURE"
	exit 0
fi

# First check if an alias was defined for this device.
ID_VDEV=$(alias_handler)

if [ -z "$ID_VDEV" ] ; then
	BAY=${BAY:-bay}
	case $TOPOLOGY in
		sas_direct|sas_switch)
			ID_VDEV=$(sas_handler)
			;;
		scsi)
			ID_VDEV=$(scsi_handler)
			;;
		*)
			echo "Error: unknown topology $TOPOLOGY"
			exit 1
			;;
	esac
fi

if [ -n "$ID_VDEV" ] ; then
	echo "ID_VDEV=${ID_VDEV}"
	echo "ID_VDEV_PATH=disk/by-vdev/${ID_VDEV}"
fi
