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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

#
# DESCRIPTION:
# Test /proc/spl/kstat/zfs/<pool>/state kstat
#
# STRATEGY:
# 1. Create a mirrored pool
# 2. Check that pool is ONLINE
# 3. Fault one disk
# 4. Check that pool is DEGRADED
# 5. Create a new pool with a single scsi_debug disk
# 6. Remove the disk
# 7. Check that pool is SUSPENDED
# 8. Add the disk back in
# 9. Clear errors and destroy the pools

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	# Destroy the scsi_debug pool
	if [ -n "$TESTPOOL2" ] ; then
		if  [ -n "$host" ] ; then
			# Re-enable the disk
			scan_scsi_hosts $host

			# Device may have changed names after being inserted
			SDISK=$(get_debug_device)
			log_must ln $DEV_RDSKDIR/$SDISK $REALDISK
		fi

		# Restore our working pool image
		if [ -n "$BACKUP" ] ; then
			gunzip -c $BACKUP > $REALDISK
			log_must rm -f $BACKUP
		fi

		if poolexists $TESTPOOL2 ; then
			# Our disk is back.  Now we can clear errors and destroy the
			# pool cleanly.
			log_must zpool clear $TESTPOOL2

			# Now that the disk is back and errors cleared, wait for our
			# hung 'zpool scrub' to finish.
			wait

			destroy_pool $TESTPOOL2
		fi
		log_must rm -f $REALDISK
		unload_scsi_debug
	fi
}

# Check that our pool state values match what's expected
#
# $1: pool name
# $2: expected state ("ONLINE", "DEGRADED", "SUSPENDED", etc)
function check_all
{
	pool=$1
	expected=$2

	state1=$(zpool status $pool | awk '/state: /{print $2}');
	state2=$(zpool list -H -o health $pool)
	state3=$(</proc/spl/kstat/zfs/$pool/state)
	log_note "Checking $expected = $state1 = $state2 = $state3"
	if [[ "$expected" == "$state1" &&  "$expected" == "$state2" && \
	    "$expected" == "$state3" ]] ; then
		true
	else
		false
	fi
}

log_onexit cleanup

log_assert "Testing /proc/spl/kstat/zfs/<pool>/state kstat"

# Test that the initial pool is healthy
check_all $TESTPOOL "ONLINE"

# Fault one of the disks, and check that pool is degraded
read -r DISK1 _ <<<"$DISKS"
log_must zpool offline -tf $TESTPOOL $DISK1
check_all $TESTPOOL "DEGRADED"
log_must zpool online $TESTPOOL $DISK1
log_must zpool clear $TESTPOOL

# Create a new pool out of a scsi_debug disk
TESTPOOL2=testpool2
MINVDEVSIZE_MB=$((MINVDEVSIZE / 1048576))
load_scsi_debug $MINVDEVSIZE_MB 1 1 1 '512b'

SDISK=$(get_debug_device)
host=$(get_scsi_host $SDISK)

# Use $REALDISK instead of $SDISK in our pool because $SDISK can change names
# as we remove/add the disk (i.e. /dev/sdf -> /dev/sdg).
REALDISK=/dev/kstat-state-realdisk
log_must [ ! -e $REALDISK ]
ln $DEV_RDSKDIR/$SDISK $REALDISK

log_must zpool create $TESTPOOL2 $REALDISK

# Backup the contents of the disk image
BACKUP=$TEST_BASE_DIR/kstat-state-realdisk.gz
log_must [ ! -e $BACKUP ]
gzip -c $REALDISK > $BACKUP

# Yank out the disk from under the pool
log_must rm $REALDISK
remove_disk $SDISK

# Run a 'zpool scrub' in the background to suspend the pool.  We run it in the
# background since the command will hang when the pool gets suspended.  The
# command will resume and exit after we restore the missing disk later on.
zpool scrub $TESTPOOL2 &
# Once we trigger the zpool scrub, all zpool/zfs command gets stuck for 180 seconds.
# Post 180 seconds zpool/zfs commands gets start executing however few more seconds(10s)
# it take to update the status.
# hence sleeping for 200 seconds so that we get the correct status.
sleep 200		# Give the scrub some time to run before we check if it fails

log_must check_all $TESTPOOL2 "SUSPENDED"

log_pass "/proc/spl/kstat/zfs/<pool>/state test successful"
