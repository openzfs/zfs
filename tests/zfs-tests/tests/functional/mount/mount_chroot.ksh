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

chroot=$TESTDIR.chroot
fs=$TESTPOOL/$TESTFS.chroot

log_must mkdir -p $chroot/dataset
log_must zfs create -o mountpoint=$chroot/dataset $fs
old_expiry=$(cat /sys/module/zfs/parameters/zfs_expire_snapshot)

function cleanup {
    zfs destroy -r "$fs"
    rm -R "$chroot"
    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
    mount | grep zfs
}

log_onexit cleanup

function test_chroot {
    local mountpoint=$1
    local dir=${chroot}$2
    local chroot_outer=$2
    local mountpoint_inner=$3
    [[ $unmount_type == auto ]] && printf "2" > /sys/module/zfs/parameters/zfs_expire_snapshot

    log_must mkdir -p "$dir/bin"
    log_must cp /bin/busybox "$dir/bin"
    log_must ln -s /bin/busybox "$dir/bin/sh"
    log_must ln -s /bin/busybox "$dir/bin/ls"
    log_must touch "${mountpoint}/testfile"
    log_must zfs snap ${fs}@snap
    log_mustnot eval "mount | grep @"

    log_must /usr/sbin/chroot ${dir} /bin/ls ${mountpoint_inner}/.zfs/snapshot/snap/testfile
    log_must ls ${mountpoint}/.zfs/snapshot/snap/testfile
    log_must eval "mount | grep @"
    unmount_automounted ${mountpoint}/.zfs/snapshot/snap
    log_mustnot eval "mount | grep @"

    log_must ls ${mountpoint}/.zfs/snapshot/snap/testfile
    log_must /usr/sbin/chroot ${dir} /bin/ls ${mountpoint_inner}/.zfs/snapshot/snap/testfile
    log_must eval "mount | grep @"

    unmount_automounted ${mountpoint}/.zfs/snapshot/snap
    log_must zfs destroy ${fs}@snap
    log_must rm -r "$dir/bin"

    printf "%s" "${old_expiry}" > /sys/module/zfs/parameters/zfs_expire_snapshot
}

unmount_type=manual
test_chroot "$chroot/dataset" "/dataset" ""
test_chroot "$chroot/dataset" "" "/dataset"
unmount_type=auto
test_chroot "$chroot/dataset" "" "/dataset"

log_pass "All ZFS file systems would have been unmounted"
