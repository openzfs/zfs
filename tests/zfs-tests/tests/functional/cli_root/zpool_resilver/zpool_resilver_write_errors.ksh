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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_resilver/zpool_resilver.cfg

#
# DESCRIPTION:
#	A resilver that follows one whose repair writes failed completes
#	and clears the attached device's DTL.  The failed writes leave
#	only txgs from before the attach in the DTL, so the second
#	resilver doesn't cover the attach txg.
#
# STRATEGY:
#	1. Create a pool with one data device and a special device, so
#	   that only file data is written to the data device, and write
#	   two one-block files in separate txgs
#	2. Attach a second device to the data device, and make all writes
#	   to it fail while the resilver runs
#	3. Check that the first device can't be detached (the new device
#	   is missing the files' blocks)
#	4. Stop the injection and resilver again
#	5. Detach the first device and scrub the pool
#

verify_runnable "global"

typeset VDEV_DATA=$TEST_BASE_DIR/vdev-data
typeset VDEV_NEW=$TEST_BASE_DIR/vdev-new
typeset VDEV_SPECIAL=$TEST_BASE_DIR/vdev-special

function cleanup
{
	zinject -c all
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV_DATA $VDEV_NEW $VDEV_SPECIAL
}

log_assert "A resilver after one with failed repair writes clears the DTL"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV_DATA $VDEV_NEW $VDEV_SPECIAL
log_must zpool create -f $TESTPOOL1 $VDEV_DATA special $VDEV_SPECIAL

# One 128k block per file, each born in its own txg.
for i in 1 2; do
	log_must dd if=/dev/urandom of=/$TESTPOOL1/file$i bs=128k count=1
	sync_pool $TESTPOOL1
done

# Fail every write to the new device; metadata goes to the special
# device, so only the resilver's repair writes reach it.
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach $TESTPOOL1 $VDEV_DATA $VDEV_NEW
log_must is_pool_resilvering $TESTPOOL1
log_must zinject -d $VDEV_NEW -e io -T write $TESTPOOL1
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1

# The resilver may have scheduled another one; check the detach while
# writes still fail, so that it can't have repaired the device yet.
typeset errs=$(zpool status -p $TESTPOOL1 | \
    awk -v d=$VDEV_NEW '$1 == d {print $4}')
log_note "write errors on $VDEV_NEW: $errs"
(( errs > 0 )) || log_fail "no write errors on $VDEV_NEW"
log_mustnot zpool detach $TESTPOOL1 $VDEV_DATA
log_must zinject -c all

# The DTL now holds only the files' txgs; this resilver covers just those.
# zpool resilver only schedules it, so sync before waiting.
log_must zpool resilver $TESTPOOL1
sync_pool $TESTPOOL1
log_must zpool wait -t resilver $TESTPOOL1

# The new device is now the only copy; the scrub reads every block.
log_must zpool detach $TESTPOOL1 $VDEV_DATA
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A resilver after one with failed repair writes clears the DTL"
