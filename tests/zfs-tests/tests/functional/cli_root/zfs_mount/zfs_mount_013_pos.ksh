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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
# Verify zfs mount helper functions for both devices and pools.
#

verify_runnable "both"

set -A vdevs $(get_disklist_fullpath $TESTPOOL)
vdev=${vdevs[0]}
mntpoint=$TESTDIR/$TESTPOOL
helper="mount.zfs -o zfsutil"
fs=$TESTPOOL/$TESTFS

function cleanup
{
    log_must force_unmount $vdev
    [[ -d $mntpoint ]] && log_must rm -rf $mntpoint
	return 0
}
log_onexit cleanup

log_note "Verify zfs mount helper functions for both devices and pools"

# Ensure that the ZFS filesystem is unmounted
force_unmount $fs
log_must mkdir -p $mntpoint

log_note "Verify '<dataset> <path>'"
log_must $helper $fs $mntpoint
log_must ismounted $fs
force_unmount $fs

log_note "Verify '\$PWD/<pool> <path>' prefix workaround"
log_must $helper $PWD/$fs $mntpoint
log_must ismounted $fs
force_unmount $fs

log_note "Verify '-f <dataset> <path>' fakemount"
log_must $helper -f $fs $mntpoint
log_mustnot ismounted $fs

log_note "Verify '-o ro -v <dataset> <path>' verbose RO"
log_must ${helper},ro -v $fs $mntpoint
log_must ismounted $fs
force_unmount $fs

log_note "Verify '<device> <path>'"
log_must $helper $vdev $mntpoint
log_must ismounted $mntpoint
log_must umount $TESTPOOL

log_note "Verify '-o abc -s <device> <path>' sloppy option"
log_must ${helper},abc -s $vdev $mntpoint
log_must ismounted $mntpoint
log_must umount $TESTPOOL

log_pass "zfs mount helper correctly handles both device and pool strings"