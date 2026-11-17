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

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A pool imports healthy from devices whose paths changed, after a
#	crash right before the uberblocks of the txg that detaches one side
#	of a mirror.  Labels 0 and 2 of the remaining devices, written before
#	those uberblocks, no longer name the detached device, but no
#	uberblock covers them: the import builds its config from labels 1
#	and 3 instead, which match the uberblock the pool is loaded from, so
#	that it finds the detached device.
#
# STRATEGY:
#	1. Create a three-way mirror, write data, export the pool and copy
#	   its devices
#	2. Import the pool, detach one device and export the pool again
#	3. Put back labels 1 and 3 and every uberblock ring of the remaining
#	   devices from their copies, leaving labels 0 and 2 from after the
#	   detach, and the detached device from its copy, labels included:
#	   what a crash right before the uberblocks of the detach txg leaves
#	   when the detached device keeps its labels
#	4. Rename the three devices, so that the paths in the labels lead
#	   nowhere
#	5. Verify that the pool imports, healthy with the three devices
#	   online at their new paths, and that the data is intact
#

verify_runnable "global"

function custom_cleanup
{
	rm -rf $BACKUP_DEVICE_DIR
	cleanup
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
	typeset copy=$BACKUP_DEVICE_DIR/$(basename $dev)
	typeset -i n=$((FILE_SIZE / 131072))

	log_must dd if=$copy of=$dev bs=128k skip=1 seek=1 count=3 \
	    conv=notrunc
	log_must dd if=$copy of=$dev bs=128k skip=$((n - 3)) seek=$((n - 3)) \
	    count=3 conv=notrunc
}

log_onexit custom_cleanup

log_assert "A pool imports healthy from renamed devices after a crash" \
    "before the uberblocks of the txg of a detach."

log_must zpool create $TESTPOOL1 mirror $VDEV0 $VDEV1 $VDEV2
log_must generate_data $TESTPOOL1 $MD5FILE
log_must zpool export $TESTPOOL1
log_must mkdir -p $BACKUP_DEVICE_DIR
log_must cp $VDEV0 $VDEV1 $VDEV2 $BACKUP_DEVICE_DIR

log_must zpool import -d $DEVICE_DIR $TESTPOOL1
log_must zpool detach $TESTPOOL1 $VDEV2
log_must zpool export $TESTPOOL1

restore_pre_detach $VDEV0
restore_pre_detach $VDEV1
log_must cp $BACKUP_DEVICE_DIR/$(basename $VDEV2) $VDEV2

log_must mv $VDEV0 $DEVICE_DIR/m0
log_must mv $VDEV1 $DEVICE_DIR/m1
log_must mv $VDEV2 $DEVICE_DIR/m2

log_must zpool import -d $DEVICE_DIR $TESTPOOL1
log_must zpool status -P $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1
for dev in m0 m1 m2; do
	log_must check_vdev_state $TESTPOOL1 $DEVICE_DIR/$dev ONLINE
done
log_must verify_data_hashsums $MD5FILE

log_pass "A pool imports healthy from renamed devices after a crash" \
    "before the uberblocks of the txg of a detach."
