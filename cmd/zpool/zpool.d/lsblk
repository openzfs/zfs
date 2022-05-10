#!/bin/sh
#
# Print some common lsblk values
#
# Any (lowercased) name symlinked to the lsblk script will be passed to lsblk
# as one of its --output names.  Here's a partial list of --output names
# from the lsblk binary:
#
# Available columns (for --output):
#        NAME  device name
#       KNAME  internal kernel device name
#     MAJ:MIN  major:minor device number
#      FSTYPE  filesystem type
#  MOUNTPOINT  where the device is mounted
#       LABEL  filesystem LABEL
#        UUID  filesystem UUID
#          RA  read-ahead of the device
#          RO  read-only device
#          RM  removable device
#       MODEL  device identifier
#        SIZE  size of the device
#       STATE  state of the device
#       OWNER  user name
#       GROUP  group name
#        MODE  device node permissions
#   ALIGNMENT  alignment offset
#      MIN-IO  minimum I/O size
#      OPT-IO  optimal I/O size
#     PHY-SEC  physical sector size
#     LOG-SEC  logical sector size
#        ROTA  rotational device
#       SCHED  I/O scheduler name
#     RQ-SIZE  request queue size
#        TYPE  device type
#    DISC-ALN  discard alignment offset
#   DISC-GRAN  discard granularity
#    DISC-MAX  discard max bytes
#   DISC-ZERO  discard zeroes data
#
# If the script is run as just 'lsblk' then print out disk size, vendor,
# and model number.


helpstr="
label:	Show filesystem label.
model:	Show disk model number.
size:	Show the disk capacity.
vendor:	Show the disk vendor.
lsblk:	Show the disk size, vendor, and model number."

script="${0##*/}"

if [ "$1" = "-h" ] ; then
        echo "$helpstr" | grep "$script:" | tr -s '\t' | cut -f 2-
        exit
fi

if [ "$script" = "lsblk" ] ; then
	list="size vendor model"
else
	list=$(echo "$script" | tr '[:upper:]' '[:lower:]')
fi

# Sometimes, UPATH ends up /dev/(null).
# That should be corrected, but for now...
# shellcheck disable=SC2154
if [ ! -b "$VDEV_UPATH" ]; then
	somepath="${VDEV_PATH}"
else
	somepath="${VDEV_UPATH}"
fi

# Older versions of lsblk don't support all these values (like SERIAL).
for i in $list ; do

	# Special case: Looking up the size of a file-based vdev can't
	# be done with lsblk.
	if [ "$i" = "size" ] && [ -f "$somepath" ] ; then
		size=$(du -h --apparent-size "$somepath" | cut -f 1)
		echo "size=$size"
		continue
	fi


	val=""
	if val=$(eval "lsblk -dl -n -o $i $somepath 2>/dev/null") ; then
		# Remove leading/trailing whitespace from value
		val=$(echo "$val" | sed -e 's/^[[:space:]]*//' \
		     -e 's/[[:space:]]*$//')
	fi
	echo "$i=$val"
done
