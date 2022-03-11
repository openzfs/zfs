#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright (c) 2016, 2017 by Intel Corporation. All rights reserved.
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Automated Auto-Online Test.
#
# STRATEGY:
# 1. Create a pool
# 2. Export a pool
# 3. Offline disk
# 4. Import pool with missing disk
# 5. Online disk
# 6. ZED polls for an event change for online disk to be automatically
#    added back to the pool.
#
# Creates a raidz1 zpool using persistent disk path names
# (ie not /dev/sdc).
#
# If loop devices are used, then a scsi_debug device is added to the pool.
# otherwise just an sd device is used as the auto-online device.
# Auto-online matches by devid.
#
verify_runnable "both"

if ! is_physical_device $DISKS; then
	log_unsupported "Unsupported disks for this test."
fi

function cleanup
{
	typeset disk

	# Replace any disk that may have been removed at failure time.
	for disk in $DISK1 $DISK2 $DISK3; do
		# Skip loop devices and devices that currently exist.
		is_loop_device $disk && continue
		is_real_device $disk && continue
		insert_disk $disk $(get_scsi_host $disk)
	done
	destroy_pool $TESTPOOL
	unload_scsi_debug
}

log_assert "Testing automated auto-online FMA test"

log_onexit cleanup

# If using the default loop devices, need a scsi_debug device for auto-online
if is_loop_device $DISK1; then
	load_scsi_debug $SDSIZE $SDHOSTS $SDTGTS $SDLUNS '512b'
	SDDEVICE=$(get_debug_device)
	SDDEVICE_ID=$(get_persistent_disk_name $SDDEVICE)
	autoonline_disks="$SDDEVICE"
else
	autoonline_disks="$DISK1 $DISK2 $DISK3"
fi

# Clear disk labels
for i in {0..2}
do
	zpool labelclear -f /dev/disk/by-id/"${devs_id[i]}"
done

if is_loop_device $DISK1; then
	# create a pool with one scsi_debug device and 3 loop devices
	log_must zpool create -f $TESTPOOL raidz1 $SDDEVICE_ID $DISK1 \
	    $DISK2 $DISK3
elif ( is_real_device $DISK1 || is_mpath_device $DISK1 ); then
	# else use the persistent names for sd devices
	log_must zpool create -f $TESTPOOL raidz1 ${devs_id[0]} \
	    ${devs_id[1]} ${devs_id[2]}
else
	log_fail "Disks are not supported for this test"
fi

# Add some data to the pool
log_must mkfile $FSIZE /$TESTPOOL/data

for offline_disk in $autoonline_disks
do
	log_must zpool export -F $TESTPOOL

	host=$(get_scsi_host $offline_disk)

	# Offline disk
	remove_disk $offline_disk

	# Reimport pool with drive missing
	log_must zpool import $TESTPOOL
	check_state $TESTPOOL "" "degraded"
	if (($? != 0)); then
		log_fail "$TESTPOOL is not degraded"
	fi

	# Clear zpool events
	log_must zpool events -c

	# Online disk
	insert_disk $offline_disk $host

	log_note "Delay for ZED auto-online"
	typeset -i timeout=0
	while true; do
		if ((timeout == $MAXTIMEOUT)); then
			log_fail "Timeout occurred"
		fi
		((timeout++))

		sleep 1
		if zpool events $TESTPOOL \
		    | grep -qF sysevent.fs.zfs.resilver_finish; then
			log_note "Auto-online of $offline_disk is complete"
			sleep 1
			break
		fi
	done

	# Validate auto-online was successful
	check_state $TESTPOOL "" "online"
	if (($? != 0)); then
		log_fail "$TESTPOOL is not back online"
	fi
	sleep 2
done
log_must zpool destroy $TESTPOOL

log_pass "Auto-online test successful"
