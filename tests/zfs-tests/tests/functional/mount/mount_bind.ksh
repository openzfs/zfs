#!/bin/ksh -p

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

#
# Copyright (c) 2020 by Felix Dörre. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mount/mount_common

bind=$TESTDIR.bind
fs=$TESTPOOL/$TESTFS.bind
old_expiry=$(cat /sys/module/zfs/parameters/zfs_expire_snapshot)

log_must mkdir -p ${bind}1
log_must mkdir -p ${bind}2
log_must zfs create -o mountpoint=${bind}1 $fs

function cleanup {
    zfs destroy -r "$fs"
    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
}

log_onexit cleanup

log_must touch ${bind}1/testfile
log_must zfs snap ${fs}@snap


function testbind {
    if [[ $unmount_type == auto ]]; then
	printf "%s" "1" > /sys/module/zfs/parameters/zfs_expire_snapshot
    fi
    

    # Scenario 1: regular bind-mount
    log_must mount --bind ${bind}1 ${bind}2
    log_must ls ${bind}1/.zfs/snapshot/snap/testfile
    log_must ls ${bind}2/.zfs/snapshot/snap/testfile
    snapshots_mounted 2
    unmount_automounted ${bind}1/.zfs/snapshot/snap
    snapshots_mounted 0

    log_must ls ${bind}2/.zfs/snapshot/snap/testfile
    log_must ls ${bind}1/.zfs/snapshot/snap/testfile
    snapshots_mounted 2
    unmount_automounted ${bind}1/.zfs/snapshot/snap
    snapshots_mounted 0

    log_must umount ${bind}1
    log_mustnot ls ${bind}1/.zfs/snapshot/snap/testfile
    log_must ls ${bind}2/.zfs/snapshot/snap/testfile
    snapshots_mounted 1
    unmount_automounted ${bind}2/.zfs/snapshot/snap
    snapshots_mounted 0

    # cleanup
    log_must umount ${bind}2
    log_must zfs mount ${fs}
    
    # Scenario 2: mark the second bind-mount as "private"
    log_must mount --bind --make-private ${bind}1 ${bind}2

    log_must ls ${bind}2/.zfs/snapshot/snap/testfile
    # TODO this currently is a limitation, but not desired
    log_mustnot ls ${bind}1/.zfs/snapshot/snap/testfile
    unmount_automounted ${bind}2/.zfs/snapshot/snap
    snapshots_mounted 0

    # And symmetric
    log_must ls ${bind}1/.zfs/snapshot/snap/testfile
    # TODO this currently is a limitation, but not desired
    log_mustnot ls ${bind}2/.zfs/snapshot/snap/testfile
    unmount_automounted ${bind}1/.zfs/snapshot/snap
    snapshots_mounted 0

    log_must umount ${bind}2

    # Scenario 3: bind-mount and remove the original one
    log_must mount --bind --make-private ${bind}1 ${bind}2
    log_must umount ${bind}1
    log_must ls ${bind}2/.zfs/snapshot/snap/testfile
    log_mustnot test -f ${bind}1/.zfs/snapshot/snap/testfile
    unmount_automounted ${bind}2/.zfs/snapshot/snap
    snapshots_mounted 0

    log_must umount ${bind}2
    log_must zfs mount ${fs}
    
    # Scenario 4: bind-mount when auto-mounted snapshot is in place
    log_must ls ${bind}1/.zfs/snapshot/snap/testfile
    log_must mount --bind ${bind}1 ${bind}2
    snapshots_mounted 1
    # TODO this is a limitation, but not desired
    log_mustnot ls ${bind}2/.zfs/snapshot/snap/testfile
    snapshots_mounted 1

    unmount_automounted ${bind}1/.zfs/snapshot/snap
    snapshots_mounted 0
    # now we are back to Scenario 1, so no reason for more tests
    log_must umount ${bind}2

    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
}

unmount_type=manual
testbind
unmount_type=auto
testbind

mount | grep zfs
log_pass "All ZFS file systems would have been unmounted"
