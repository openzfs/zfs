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
# 'zpool detach' of a hot spare in use labels it as an available spare
# only once the txg that detaches it is on disk.  After a crash before
# that txg, the pool, in which the spare is still in use on disk, imports
# healthy in place.
#
# STRATEGY:
# 1. Create a mirror with a hot spare on file vdevs, write a file, and
#    replace one side of the mirror with the spare
# 2. Detach the spare while the txg of the detach is suspended, and copy
#    the devices meanwhile: what a crash before that txg leaves on disk
# 3. Destroy the pool and put the copies back in place of its devices
# 4. Verify that the pool imports, healthy with the spare in use and
#    online, the file intact, and scrubs without errors
#

verify_runnable "global"

DEVDIR=$TEST_BASE_DIR/detach_spare_crash
COPYDIR=$TEST_BASE_DIR/detach_spare_crash_copy

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL1 && zpool clear $TESTPOOL1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	restore_tunable TXG_TIMEOUT
	rm -rf $DEVDIR $COPYDIR
}

log_assert "A crash in 'zpool detach' of a hot spare before its txg is on" \
    "disk leaves a pool that imports healthy."
log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

log_must mkdir -p $DEVDIR $COPYDIR
for dev in m1 m2 s; do
	log_must truncate -s $MINVDEVSIZE $DEVDIR/$dev
done
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create $TESTPOOL1 mirror $DEVDIR/m1 $DEVDIR/m2 \
    spare $DEVDIR/s
log_must dd if=/dev/urandom of=/$TESTPOOL1/file bs=1M count=8
typeset sum=$(xxh128digest /$TESTPOOL1/file)
log_must zpool replace -w $TESTPOOL1 $DEVDIR/m2 $DEVDIR/s
log_must check_hotspare_state $TESTPOOL1 $DEVDIR/s INUSE
log_must zpool sync $TESTPOOL1

detach_suspended_copy $TESTPOOL1 $DEVDIR/s "$DEVDIR/m1 $DEVDIR/m2" \
    $COPYDIR $DEVDIR/m1 $DEVDIR/m2 $DEVDIR/s

log_must zpool destroy $TESTPOOL1
for dev in m1 m2 s; do
	log_must mv $COPYDIR/$dev $DEVDIR/$dev
done

log_must zpool import -d $DEVDIR $TESTPOOL1
log_must zpool status -P $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"
for dev in m1 m2 s; do
	log_must check_vdev_state $TESTPOOL1 $DEVDIR/$dev ONLINE
done
log_must check_hotspare_state $TESTPOOL1 $DEVDIR/s INUSE
log_must [ "$(xxh128digest /$TESTPOOL1/file)" = "$sum" ]
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A crash in 'zpool detach' of a hot spare before its txg is on" \
    "disk leaves a pool that imports healthy."
