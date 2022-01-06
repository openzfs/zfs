#!/bin/ksh -p

#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# Sequential reconstruction (unlike healing reconstruction) operate on the
# top-level vdev.  This means that a sequential resilver operation can be
# started/stopped on a different top-level vdev without impacting other
# sequential resilvers.
#
# STRATEGY:
# 1. Create a mirrored pool.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2
}

function check_history
{
	pool=$1
	msg=$2
	exp=$3

	count=$(zpool history -i $pool | grep "rebuild" | grep -c "$msg")
	if [[ "$count" -ne "$exp" ]]; then
		log_fail "Expected $exp rebuild '$msg' messages, found $count"
	else
		log_note "Found $count/$exp rebuild '$msg' messages"
	fi
}

log_assert "Rebuilds operate on the top-level vdevs"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[@]} \
    $SPARE_VDEV_FILE $SPARE_VDEV_FILE2

# Verify two sequential resilvers can run concurrently.
log_must zpool create -f $TESTPOOL1 \
    mirror ${VDEV_FILES[0]} ${VDEV_FILES[1]} \
    mirror ${VDEV_FILES[2]} ${VDEV_FILES[3]}
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=32
sync_pool $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[3]} $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "started" 2
check_history $TESTPOOL1 "reset" 0
check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 0

log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
log_must zpool wait -t resilver $TESTPOOL1

check_history $TESTPOOL1 "complete" 2
destroy_pool $TESTPOOL1

# Verify canceling one resilver (zpool detach) does not impact others.
log_must zpool create -f $TESTPOOL1 \
    mirror ${VDEV_FILES[0]} ${VDEV_FILES[1]} \
    mirror ${VDEV_FILES[2]} ${VDEV_FILES[3]}
log_must zfs create $TESTPOOL1/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=32
sync_pool $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[1]} $SPARE_VDEV_FILE
log_must zpool replace -s $TESTPOOL1 ${VDEV_FILES[3]} $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "started" 2
check_history $TESTPOOL1 "reset" 0
check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 0

log_must zpool detach $TESTPOOL1 $SPARE_VDEV_FILE2

check_history $TESTPOOL1 "complete" 0
check_history $TESTPOOL1 "canceled" 1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
log_must zpool wait -t resilver $TESTPOOL1

check_history $TESTPOOL1 "complete" 1
check_history $TESTPOOL1 "canceled" 1
destroy_pool $TESTPOOL1

log_pass "Rebuilds operate on the top-level vdevs"
