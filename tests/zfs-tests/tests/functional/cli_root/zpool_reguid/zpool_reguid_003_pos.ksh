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
# A pool still imports, with its old GUID, after a crash in the middle of
# the txg that writes a new one: the labels that already carry the new GUID
# are newer than the uberblock, and are ignored.
#
# STRATEGY:
# 1. Create a pool on file vdevs, export it and copy the devices
# 2. Import the pool, change its GUID, export it again
# 3. Put back labels 1 and 3 and every uberblock ring from the copies,
#    leaving labels 0 and 2 with the new GUID: what a crash right before
#    the uberblocks of the reguid txg leaves on disk
# 4. Verify that the pool imports, healthy, with its old GUID
#

verify_runnable "global"

VDEV_DIR=$TEST_BASE_DIR/reguid_crash
PRE_DIR=$VDEV_DIR/pre
VDEVS="vdev0 vdev1 vdev2"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR
}

#
# Copy back what a crash before the uberblocks of the reguid txg leaves
# untouched, in 128 KiB blocks: the uberblock ring of label 0 (block 1),
# label 1 (blocks 2-3), the uberblock ring of label 2 and label 3 (the last
# three blocks).
#
function restore_pre_reguid # <device>
{
	typeset dev=$1
	typeset -i n=$((MINVDEVSIZE / 131072))

	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k \
	    skip=1 seek=1 count=3 conv=notrunc
	log_must dd if=$PRE_DIR/$dev of=$VDEV_DIR/$dev bs=128k \
	    skip=$((n - 3)) seek=$((n - 3)) count=3 conv=notrunc
}

log_assert "A pool imports after a crash in the middle of 'zpool reguid'."
log_onexit cleanup

log_must mkdir -p $PRE_DIR
for dev in $VDEVS; do
	log_must truncate -s $MINVDEVSIZE $VDEV_DIR/$dev
done
log_must zpool create $TESTPOOL1 mirror $VDEV_DIR/vdev0 $VDEV_DIR/vdev1 \
    $VDEV_DIR/vdev2
old_guid=$(zpool get -H -o value guid $TESTPOOL1)
log_must zpool export $TESTPOOL1
for dev in $VDEVS; do
	log_must cp $VDEV_DIR/$dev $PRE_DIR/$dev
done

log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must zpool reguid $TESTPOOL1
new_guid=$(zpool get -H -o value guid $TESTPOOL1)
[[ "$new_guid" != "$old_guid" ]] || log_fail "the GUID did not change"
log_must zpool export $TESTPOOL1

for dev in $VDEVS; do
	restore_pre_reguid $dev
done

log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "state" "ONLINE"
guid=$(zpool get -H -o value guid $TESTPOOL1)
[[ "$guid" == "$old_guid" ]] || \
    log_fail "GUID $guid after the import, expected $old_guid"
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A pool imports after a crash in the middle of 'zpool reguid'."
