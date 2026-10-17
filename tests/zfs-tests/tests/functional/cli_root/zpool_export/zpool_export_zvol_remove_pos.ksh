#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that zpool export removes the device nodes of all the pool's
#	zvols and their snapshots, and that import creates them again.
#
# STRATEGY:
#	1. Create a pool with zvols in both volmodes and a snapshot of each,
#	   with snapdev=visible
#	2. Twice:
#	   a. Export the pool and check that none of its device nodes is left
#	   b. Import the pool and check that all of them are back
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_export-zvol-remove
NUM_VOLS=40
NUM_DEVS=$((NUM_VOLS * 2))

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -rf $DEVICE_DIR
}

# Count the pool's zvol and snapshot device nodes.
function count_devices
{
	typeset -i i count=0
	typeset dev

	for (( i = 1; i <= NUM_VOLS; i++ )); do
		for dev in $ZVOL_DEVDIR/$TESTPOOL2/vol$i \
		    $ZVOL_DEVDIR/$TESTPOOL2/vol$i@snap; do
			is_disk_device $dev && (( count += 1 ))
		done
	done
	echo $count
}

# Device nodes are created asynchronously, so wait for all of them.
function wait_for_devices
{
	typeset -i try n=0

	for (( try = 0; try < 30; try++ )); do
		block_device_wait
		n=$(count_devices)
		(( n == NUM_DEVS )) && return
		sleep 1
	done
	log_fail "only $n of $NUM_DEVS device nodes exist"
}

log_assert "zpool export removes all zvol device nodes"

log_onexit cleanup

log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE $DEVICE_DIR/disk
log_must zpool create -f -O mountpoint=none -O snapdev=visible \
    $TESTPOOL2 $DEVICE_DIR/disk
for (( i = 1; i <= NUM_VOLS; i++ )); do
	if (( i % 2 )); then
		volmode=dev
	else
		volmode=default
	fi
	log_must zfs create -s -V 8M -o volmode=$volmode $TESTPOOL2/vol$i
done
log_must zfs snapshot -r $TESTPOOL2@snap
wait_for_devices

for i in 1 2; do
	# Export removes the zvols before it returns. Wait for udev or GEOM
	# to finish removing their device nodes.
	log_must_busy zpool export $TESTPOOL2
	block_device_wait
	n=$(count_devices)
	(( n == 0 )) || log_fail "$n device nodes left after export"

	log_must zpool import -d $DEVICE_DIR $TESTPOOL2
	wait_for_devices
done

log_pass "zpool export removes all zvol device nodes"
