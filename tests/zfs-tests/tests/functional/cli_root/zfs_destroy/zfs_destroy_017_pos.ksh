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
# Copyright (c) 2024 by Chris Simons <chris@simons.network>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
# Verify that 'zfs destroy' with a type filter only destroys the datasets
# of the specified type.
#
# STRATEGY:
# 1. Create various types
# 2. Attempt to destroy with correct and incorrect types
# 3. Verify only expected items are destroyed
# 4. Ensure recursive destruction still occurs
#

function cleanup
{
    for ds in $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTVOL; do
        datasetexists $ds && log_must zfs destroy -r $ds
    done
}

log_assert "Verify 'zfs destroy' with a type filter only affects specified type"
log_onexit cleanup

# Create a filesystem, dataset and a volume
log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS1/testdataset
log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL

# Take a snapshots
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap
log_must zfs snapshot $TESTPOOL/$TESTVOL@snap
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap_del
log_must zfs snapshot $TESTPOOL/$TESTVOL@snap_del
log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap_recursive

# Destroy only snapshots with the type filter
log_mustnot zfs destroy -t snapshot $TESTPOOL/$TESTFS1
log_mustnot zfs destroy -t snapshot -r $TESTPOOL/$TESTFS1
log_mustnot zfs destroy -t snapshot $TESTPOOL/$TESTVOL
log_mustnot zfs destroy -t snapshot -r $TESTPOOL/$TESTVOL
log_must zfs destroy -t snapshot $TESTPOOL/$TESTFS1@snap_del
log_must zfs destroy -t snapshot $TESTPOOL/$TESTVOL@snap_del
log_must zfs destroy -t snapshot -r $TESTPOOL/$TESTFS1@snap_recursive

# Verify the filesystem snapshot is destroyed and the volume snapshot remains
log_must datasetexists $TESTPOOL/$TESTFS1
log_must datasetexists $TESTPOOL/$TESTVOL
log_mustnot datasetexists $TESTPOOL/$TESTFS1@snap_del
log_mustnot datasetexists $TESTPOOL/$TESTVOL@snap_del
log_mustnot datasetexists $TESTPOOL/$TESTVOL@snap_recursive
log_mustnot datasetexists $TESTPOOL/$TESTVOL/testdataset@snap_recursive

log_pass "zfs destroy with a snapshot filter only affects specified type"

