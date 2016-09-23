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
# Copyright (c) 2016 by Intel Corporation. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Tesing auto-online FMA ZED logic.
#
# STRATEGY:
# 1. Create a pool
# 2. export a pool
# 3. offline disk
# 4. import pool with missing disk
# 5. online disk
# 6. ZED polls for an event change for online disk to be automatically
#    added back to the pool.
# 7. Creates a raidz1 zpool using persistent disk path names
#    (ie not /dev/sdc).
# 8. Tests import using pool guid and cache file.
#
# If loop devices are used, then a scsi_debug device is added to the pool.
#
verify_runnable "both"

if ! is_physical_device $DISKS; then
	log_unsupported "Unsupported disks for this test."
fi

function cleanup
{
	#online last disk before fail
	on_off_disk $offline_disk "online"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Testing auto-online FMA ZED logic"

log_onexit cleanup

target=$TESTPOOL

if is_loop_device $DISK1; then
	SD=$($LSSCSI | $NAWK '/scsi_debug/ {print $6; exit}')
	SDDEVICE=$($ECHO $SD | $NAWK -F / '{print $3}')
	SDDEVICE_ID=$(get_persistent_disk_name $SDDEVICE)
	autoonline_disks="$SDDEVICE"
else
	autoonline_disks="$DISK1 $DISK2 $DISK3"
fi

# Clear disk labels
for i in {0..2}
do
	log_must $ZPOOL labelclear -f /dev/disk/by-id/"${devs_id[i]}"
done

if is_loop_device $DISK1; then
	#create a pool with one scsi_debug device and 3 loop devices
	log_must $ZPOOL create -f $TESTPOOL raidz1 $SDDEVICE_ID $DISK1 \
	    $DISK2 $DISK3
elif ( is_real_device $DISK1 || is_mpath_device $DISK1 ); then
	log_must $ZPOOL create -f $TESTPOOL raidz1 ${devs_id[0]} \
	    ${devs_id[1]} ${devs_id[2]}
else
	log_fail "Disks are not supported for this test"
fi

#add some data to the pool
log_must $MKFILE $FSIZE /$TESTPOOL/data

#pool guid import
typeset guid=$(get_config $TESTPOOL pool_guid)
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
fi

for offline_disk in $autoonline_disks
do
	log_must $ZPOOL export -F $TESTPOOL

	host=$($LS /sys/block/$offline_disk/device/scsi_device | $NAWK -F : '{ print $1}')
	#offline disk
	on_off_disk $offline_disk "offline"

	#reimport pool with drive missing
	log_must $ZPOOL import $target
	check_state $TESTPOOL "" "degraded"
	if (($? != 0)); then
		log_fail "$TESTPOOL is not degraded"
	fi

	#online disk
	on_off_disk $offline_disk "online" $host
	
	log_note "Delay for ZED auto-online"
	typeset -i timeout=0
	$CAT ${ZEDLET_DIR}/zedlog | \
	    $EGREP "zfs_iter_vdev: matched devid" > /dev/null
	while (($? != 0)); do
		if ((timeout == $MAXTIMEOUT)); then
			log_fail "Timeout occured"
		fi
		((timeout++))
		$SLEEP 1
		$CAT ${ZEDLET_DIR}/zedlog | \
		    $EGREP "zfs_iter_vdev: matched devid" > /dev/null
	done

	check_state $TESTPOOL "" "online"
	if (($? != 0)); then
		log_fail "$TESTPOOL is not back online"
	fi
	$SLEEP 2
done
log_must $ZPOOL destroy $TESTPOOL


log_pass "Auto-online test successful"
