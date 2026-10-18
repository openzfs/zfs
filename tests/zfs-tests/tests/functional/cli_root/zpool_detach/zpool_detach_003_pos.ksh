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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A pool imports healthy from devices that have moved after a crash in
# 'zpool detach' right after the labels of the detached device were
# erased: those labels, left with txg 0, still lead the import to the
# device, which the pool on disk has not detached yet.
#
# STRATEGY:
# 1. Create a mirror on file vdevs, export it and copy the devices
# 2. Import the pool, detach one device, export it again
# 3. Put back every label and uberblock ring of the remaining device from
#    its copy, leaving the detached device with its erased labels: what a
#    crash before the first label of the detach txg leaves
# 4. Rename the devices and move them to another directory
# 5. Verify that the pool imports from there, healthy with both devices
#    online, and scrubs without errors
#

verify_runnable "global"

VDEV_DIR=$TEST_BASE_DIR/detach_erased
PRE_DIR=$VDEV_DIR/pre
MOVED_DIR=$TEST_BASE_DIR/detach_erased_moved

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR $MOVED_DIR
}

#
# Copy back the four labels with their uberblock rings, in 128 KiB blocks:
# labels 0 and 1 are the first four blocks, labels 2 and 3 the last four.
#
function restore_labels # <device>
{
	typeset dev=$1
	typeset -i n=$((MINVDEVSIZE / 131072))

	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k count=4 \
	    conv=notrunc
	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k \
	    skip=$((n - 4)) seek=$((n - 4)) count=4 conv=notrunc
}

log_assert "A pool imports healthy from moved devices after a crash in" \
    "'zpool detach' right after the detached device was erased."
log_onexit cleanup

log_must mkdir -p $PRE_DIR $MOVED_DIR
for dev in vdev0 vdev1; do
	log_must truncate -s $MINVDEVSIZE $VDEV_DIR/$dev
done
log_must zpool create $TESTPOOL1 mirror $VDEV_DIR/vdev0 $VDEV_DIR/vdev1
log_must zpool export $TESTPOOL1
log_must cp $VDEV_DIR/vdev0 $PRE_DIR/vdev0

log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must zpool detach $TESTPOOL1 $VDEV_DIR/vdev1
log_must zpool export $TESTPOOL1

restore_labels vdev0
for dev in vdev0 vdev1; do
	log_must mv $VDEV_DIR/$dev $MOVED_DIR/moved_$dev
done

log_must zpool import -d $MOVED_DIR $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"
for dev in vdev0 vdev1; do
	log_must check_vdev_state $TESTPOOL1 $MOVED_DIR/moved_$dev ONLINE
done
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A pool imports healthy from moved devices after a crash in" \
    "'zpool detach' right after the detached device was erased."
