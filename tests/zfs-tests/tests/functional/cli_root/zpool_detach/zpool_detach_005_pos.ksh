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

. $STF_SUITE/tests/functional/cli_root/zpool_detach/zpool_detach.kshlib

#
# DESCRIPTION:
# 'zpool detach' erases the labels of the detached device only once the
# txg that detaches it is on disk.  After a crash before that txg, the
# pool, still a three-way mirror on disk, imports healthy from devices
# that have moved, the detached one included.
#
# STRATEGY:
# 1. Create a three-way mirror on file vdevs and write a file
# 2. Detach one side while the txg of the detach is suspended, and copy
#    the devices to another directory meanwhile: what a crash before that
#    txg leaves on disk
# 3. Verify that the detach then completes and erases the labels of the
#    detached device
# 4. Destroy the pool and remove its devices
# 5. Verify that the pool imports from the copies, healthy with the three
#    devices online, the file intact, and scrubs without errors
#
# NOTES:
# A two-way mirror does not work: the remaining side takes over the guid
# of the mirror, and the error handlers set on it stop matching.
#

verify_runnable "global"

DEVDIR=$TEST_BASE_DIR/detach_crash
COPYDIR=$TEST_BASE_DIR/detach_crash_copy

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL1 && zpool clear $TESTPOOL1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	restore_tunable TXG_TIMEOUT
	rm -rf $DEVDIR $COPYDIR
}

log_assert "A crash in 'zpool detach' before its txg is on disk leaves a" \
    "pool that imports healthy from moved devices."
log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

log_must mkdir -p $DEVDIR $COPYDIR
for dev in m1 m2 m3; do
	log_must truncate -s $MINVDEVSIZE $DEVDIR/$dev
done
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create $TESTPOOL1 mirror $DEVDIR/m1 $DEVDIR/m2 $DEVDIR/m3
log_must dd if=/dev/urandom of=/$TESTPOOL1/file bs=1M count=8
typeset sum=$(xxh128digest /$TESTPOOL1/file)
log_must zpool sync $TESTPOOL1

detach_suspended_copy $TESTPOOL1 $DEVDIR/m3 "$DEVDIR/m1 $DEVDIR/m2" \
    $COPYDIR $DEVDIR/m1 $DEVDIR/m2 $DEVDIR/m3
log_must [ "$(label_txgs $DEVDIR/m3 | sort -u)" = "0" ]
log_note "txg of the labels of the copy of m3: $(label_txgs $COPYDIR/m3)"

log_must zpool destroy $TESTPOOL1
log_must rm -f $DEVDIR/m1 $DEVDIR/m2 $DEVDIR/m3

log_must zpool import -d $COPYDIR $TESTPOOL1
log_must zpool status -P $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"
for dev in m1 m2 m3; do
	log_must check_vdev_state $TESTPOOL1 $COPYDIR/$dev ONLINE
done
log_must [ "$(xxh128digest /$TESTPOOL1/file)" = "$sum" ]
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A crash in 'zpool detach' before its txg is on disk leaves a" \
    "pool that imports healthy from moved devices."
