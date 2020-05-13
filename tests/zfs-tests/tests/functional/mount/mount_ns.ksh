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

mountpoint=$TESTDIR.ns
fs=$TESTPOOL/$TESTFS.ns

log_must zfs create -o mountpoint=${mountpoint} $fs
mntns=0
old_expiry=$(cat /sys/module/zfs/parameters/zfs_expire_snapshot)

function cleanup {
    (( mntns != 0 )) && kill $mntns
    zfs destroy -r "$fs"
    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
}

log_onexit cleanup

log_must touch ${mountpoint}/testfile
log_must zfs snap ${fs}@snap

function snaps_inside {
    /usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt mount | grep -F "$fs@" | wc -l
}

/usr/bin/unshare -m bash -c "while :; do sleep 5; done" &
mntns=$!
assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

log_must ls ${mountpoint}/.zfs/snapshot/snap/testfile
assert $(snaps_outside) == 1
assert $(snaps_inside) == 0
log_must umount ${mountpoint}/.zfs/snapshot/snap
assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

log_must /usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt ls ${mountpoint}/.zfs/snapshot/snap/testfile
assert $(snaps_outside) == 0
assert $(snaps_inside) == 1

# TODO this is not desired behavior
log_mustnot ls ${mountpoint}/.zfs/snapshot/snap/testfile
assert $(snaps_outside) == 0
assert $(snaps_inside) == 1

log_must /usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt umount ${mountpoint}/.zfs/snapshot/snap
assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

kill ${mntns}
mntns=0
assert $(snaps_outside) == 0

# And now the same, but this time the snapshot is automounted before we fork of the mount ns
printf "%s" "5" > /sys/module/zfs/parameters/zfs_expire_snapshot

# Just quickly verify that auto unmounting works
log_must ls ${mountpoint}/.zfs/snapshot/snap/testfile
assert $(snaps_outside) == 1
mount | grep zfs
unmount_type=auto
unmount_automounted ${mountpoint}/.zfs/snapshot/snap
mount | grep zfs
printf "should have expired now, checking...\n"
assert $(snaps_outside) == 0


log_must ls ${mountpoint}/.zfs/snapshot/snap/testfile
/usr/bin/unshare -m bash -c "while :; do sleep 5; done" &
mntns=$!
mount | grep -F "$fs@"
/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt mount | grep -F "$fs@"
assert $(snaps_outside) == 1
assert $(snaps_inside) == 1

# now expire all mounts
sleep 15
printf "outer\n"
mount | grep -F "$fs@"
printf "inner\n"
/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt mount | grep -F "$fs@"
assert $(snaps_outside) == 0
# TODO this is undesired
assert $(snaps_inside) == 0
#log_must /usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt umount /var/tmp/testdir.ns/.zfs/snapshot/snap

assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

log_pass "All ZFS file systems would have been unmounted"
