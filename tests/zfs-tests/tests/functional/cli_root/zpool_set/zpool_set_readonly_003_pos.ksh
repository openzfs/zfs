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
#
# zpool set readonly=on succeeds with unmounted datasets and closed zvols
#
# STRATEGY:
# 1. Create a pool with a mounted filesystem, an unmounted filesystem, and
#    a closed zvol
# 2. Remount the mounted filesystem readonly
# 3. Verify zpool set readonly=on succeeds
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL1/$TESTFS && \
	    ismounted $TESTPOOL1/$TESTFS && \
	    log_must zfs unmount -f $TESTPOOL1/$TESTFS
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV
}

log_assert "zpool set readonly=on succeeds with unmounted datasets and closed zvols"
log_onexit cleanup

FILEVDEV="$TEST_BASE_DIR/zpool_set_readonly_003.$$.dat"
UNMOUNTED=$TESTPOOL1/${TESTFS}_unmounted
VOL=$TESTPOOL1/$TESTVOL

log_must truncate -s $MINVDEVSIZE $FILEVDEV
log_must zpool create -O canmount=off $TESTPOOL1 $FILEVDEV
log_must zfs create $TESTPOOL1/$TESTFS
log_must zfs create $UNMOUNTED
log_must zfs unmount $UNMOUNTED
log_must zfs create -V 10m $VOL
block_device_wait $ZVOL_DEVDIR/$VOL

log_must zfs mount -o remount,ro $TESTPOOL1/$TESTFS
log_must zpool set readonly=on $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" == "on"

log_pass "zpool set readonly=on succeeds with unmounted datasets and closed zvols"
