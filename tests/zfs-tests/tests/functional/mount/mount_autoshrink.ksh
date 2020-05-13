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

dir=$TESTDIR.autoshrink
fs=$TESTPOOL/$TESTFS.autoshrink

log_must mkdir -p "$dir"
log_must zfs create -o mountpoint=$dir $fs

function cleanup {
    zfs destroy -r "$fs"
    rm -R "$dir"
}

log_onexit cleanup

log_must touch "$dir/testfile"
log_must zfs snap ${fs}@snap

log_must ls ${dir}/.zfs/snapshot/snap/testfile
log_must umount ${dir}

log_must zfs mount ${fs}
log_must ls ${dir}/.zfs/snapshot/snap/testfile
log_must zfs umount ${fs}

log_pass "All ZFS file systems would have been unmounted"
