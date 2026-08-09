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
#
# Copyright (c) 2024 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION: Verify that raidz children vdev fault count is restricted
#
# STRATEGY:
# 1. Create a raidz2 or raidz3 pool and add some data to it
# 2. Replace one of the child vdevs to create a replacing vdev
# 3. While it is resilvering, attempt to fault disks
# 4. Verify that less than parity count was faulted while replacing
#

TESTPOOL="fault-test-pool"
PARITY=$((RANDOM%(2) + 2))
VDEV_CNT=$((4 + (2 * PARITY)))
VDEV_SIZ=512M

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$((VDEV_CNT - 1))}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

log_onexit cleanup
log_assert "restricts raidz children vdev fault count"

log_note "creating $VDEV_CNT vdevs for parity $PARITY test"
typeset -a disks
for i in {0..$((VDEV_CNT - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s $VDEV_SIZ $device
	disks[${#disks[*]}+1]=$device
done

log_must zpool create -f ${TESTPOOL} raidz${PARITY} ${disks[1..$((VDEV_CNT - 1))]}

# Add some data to the pool
log_must zfs create $TESTPOOL/fs
MNTPOINT="$(get_prop mountpoint $TESTPOOL/fs)"
log_must fill_fs $MNTPOINT $PARITY 200 32768 100 R
sync_pool $TESTPOOL

# Replace the last child vdev to form a replacing vdev
log_must zpool replace ${TESTPOOL} ${disks[$((VDEV_CNT - 1))]} ${disks[$VDEV_CNT]}
# imediately offline replacement disk to keep replacing vdev around
log_must zpool offline ${TESTPOOL} ${disks[$VDEV_CNT]}

# Fault disks while a replacing vdev is still active
for disk in ${disks[0..$PARITY]}; do
	log_must zpool offline -tf ${TESTPOOL} $disk
done

zpool status $TESTPOOL

# Count the faults that succeeded
faults=0
for disk in ${disks[0..$PARITY]}; do
	state=$(zpool get -H -o value state ${TESTPOOL} ${disk})
	if [ "$state" = "FAULTED" ] ; then
		((faults=faults+1))
	fi
done

log_must test "$faults" -lt "$PARITY"
log_must test "$faults" -gt 0

log_pass "restricts raidz children vdev fault count"
