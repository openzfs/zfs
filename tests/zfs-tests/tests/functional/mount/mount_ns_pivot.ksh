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

mountpoint=$TESTDIR.ns_pivot
fs=$TESTPOOL/$TESTFS.ns_pivot

log_must zfs create -o mountpoint=${mountpoint} $fs
mntns=0
old_expiry=$(cat /sys/module/zfs/parameters/zfs_expire_snapshot)

function cleanup {
    (( mntns != 0 )) && kill $mntns
    zfs destroy -r "$fs"
    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
}

log_onexit cleanup

log_must mkdir "${mountpoint}/bin"
log_must mkdir "${mountpoint}/mnt"
log_must mkdir "${mountpoint}/proc"
log_must cp /bin/busybox ${mountpoint}/bin/busybox
log_must ln -s /bin/busybox ${mountpoint}/bin/sh
log_must ln -s /bin/busybox ${mountpoint}/bin/mount
log_must ln -s /bin/busybox ${mountpoint}/bin/umount
log_must ln -s /bin/busybox ${mountpoint}/bin/ls
log_must ln -s /bin/busybox ${mountpoint}/bin/pivot_root
log_must zfs snap ${fs}@snap

function snaps_inside {
    /usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt /bin/mount | grep -F "$fs@" | wc -l
}

/usr/bin/unshare -m $STF_SUITE/tests/functional/mount/mount_ns_pivot_init "${mountpoint}" &
mntns=$!
echo ${mountns}
while ! [[ -e ${mountpoint}/ready ]]; do
    sleep 1
done
assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt /bin/ls /.zfs/snapshot/snap/bin/busybox
assert $(snaps_outside) == 0
assert $(snaps_inside) == 1

/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt /bin/umount /.zfs/snapshot/snap
assert $(snaps_outside) == 0
assert $(snaps_inside) == 0

printf "5" > /sys/module/zfs/parameters/zfs_expire_snapshot

/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt /bin/ls /.zfs/snapshot/snap/bin/busybox
assert $(snaps_outside) == 0
assert $(snaps_inside) == 1
for i in {0..20}; do
    (( $(snaps_inside) == 0 )) && break
    sleep 1
done
assert $(snaps_outside) == 0
# TODO this is undesired.
assert $(snaps_inside) == 0
#/usr/bin/nsenter --mount=/proc/${mntns}/ns/mnt /bin/umount /.zfs/snapshot/snap

log_pass "All ZFS file systems would have been unmounted"
