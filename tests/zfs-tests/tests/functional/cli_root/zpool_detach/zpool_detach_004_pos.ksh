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
# A pool imports healthy, in place, after a crash in the middle of the txg
# that detaches one side of a mirror.  The newest labels of the remaining
# device already describe it as the top-level vdev, with the guid of the
# mirror, but they are newer than the uberblock the pool is loaded from:
# the device is validated against its older labels, which match the
# configuration of that uberblock.
#
# STRATEGY:
# 1. Create a mirror on file vdevs, write a file, export the pool and
#    copy the device that is to remain
# 2. Import the pool, detach the other device, export it again
# 3. Put back labels 1 and 3 and every uberblock ring of the remaining
#    device from its copy, leaving labels 0 and 2 from after the detach:
#    what a crash right before the uberblocks of the detach txg leaves
# 4. Verify that the pool imports in place, healthy with both devices
#    online, that the file reads back unchanged, and that the pool scrubs
#    without errors
#

verify_runnable "global"

VDEV_DIR=$TEST_BASE_DIR/detach_crash_inplace
PRE_DIR=$VDEV_DIR/pre

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR
}

#
# Copy back what a crash before the uberblocks of the detach txg leaves
# untouched, in 128 KiB blocks: the uberblock ring of label 0 (block 1),
# label 1 (blocks 2-3), the uberblock ring of label 2 and label 3 (the last
# three blocks).
#
function restore_pre_detach # <device>
{
	typeset dev=$1
	typeset -i n=$((MINVDEVSIZE / 131072))

	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k \
	    skip=1 seek=1 count=3 conv=notrunc
	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k \
	    skip=$((n - 3)) seek=$((n - 3)) count=3 conv=notrunc
}

log_assert "A pool imports healthy in place after a crash in the middle" \
    "of 'zpool detach'."
log_onexit cleanup

log_must mkdir -p $PRE_DIR
for dev in vdev0 vdev1; do
	log_must truncate -s $MINVDEVSIZE $VDEV_DIR/$dev
done
log_must zpool create $TESTPOOL1 mirror $VDEV_DIR/vdev0 $VDEV_DIR/vdev1
mntpnt=$(get_prop mountpoint $TESTPOOL1)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=8
sum=$(xxh128digest $mntpnt/file)
log_must zpool export $TESTPOOL1
log_must cp $VDEV_DIR/vdev0 $PRE_DIR/vdev0

log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must zpool detach $TESTPOOL1 $VDEV_DIR/vdev1
log_must zpool export $TESTPOOL1

restore_pre_detach vdev0

log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"
for dev in vdev0 vdev1; do
	log_must check_vdev_state $TESTPOOL1 $VDEV_DIR/$dev ONLINE
done
[[ "$(xxh128digest $mntpnt/file)" == "$sum" ]] || \
    log_fail "$mntpnt/file changed across the detach and the import"
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A pool imports healthy in place after a crash in the middle" \
    "of 'zpool detach'."
