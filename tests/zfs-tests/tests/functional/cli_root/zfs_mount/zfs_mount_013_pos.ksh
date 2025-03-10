#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
typeset -r mntpoint=$(get_prop mountpoint $TESTPOOL)
typeset -r helper="mount.zfs -o zfsutil"
typeset -r fs=$TESTPOOL/$TESTFS

function cleanup
{
	cd $STF_SUITE
	if [[ -d $TESTDIR/$$ ]]; then
		log_must rm -rf $TESTDIR/$$
	fi
	mounted && zfs $mountcmd $TESTPOOL
	return 0
}
log_onexit cleanup

log_note "Verify zfs mount helper functions for both devices and pools"

# Ensure that the ZFS filesystem is unmounted
force_unmount $TESTPOOL

log_note "Verify '<dataset> <path>'"
log_must $helper $fs $mntpoint
log_must ismounted $fs
force_unmount $fs

log_note "Verify mount(8) does not canonicalize before calling helper"
# Canonicalization is confused by files in PWD matching [device|mountpoint]
log_must mkdir -p $TESTDIR/$$/$TESTPOOL
log_must cd $TESTDIR/$$
# The env flag directs zfs to exec /bin/mount, which then calls helper
log_must eval ZFS_MOUNT_HELPER=1 zfs $mountcmd -v $TESTPOOL
# mount (2.35.2) still suffers from a cosmetic PWD prefix bug
log_must mounted $TESTPOOL
force_unmount $TESTPOOL

log_note "Verify CWD prefix filter <dataset> <path>"
log_must cd /
log_must zfs set mountpoint=legacy $TESTPOOL
log_must mkdir -p $mntpoint
log_must mount -t zfs $TESTPOOL $mntpoint
log_must ismounted $TESTPOOL
log_must umount $mntpoint
log_must zfs set mountpoint=$mntpoint $TESTPOOL
log_must cd -
force_unmount $TESTPOOL

log_note "Verify '-f <dataset> <path>' fakemount"
log_must $helper -f $fs $mntpoint
log_mustnot ismounted $fs

log_note "Verify '-o ro -v <dataset> <path>' verbose RO"
log_must ${helper},ro -v $fs $mntpoint
log_must ismounted $fs
force_unmount $fs

log_note "Verify '-o abc -s <device> <path>' sloppy option"
log_must ${helper},abc -s ${vdevs[0]} $mntpoint
log_must mounted $mntpoint
force_unmount $TESTPOOL

log_note "Verify '<device> <path>'"
log_must $helper ${vdevs[0]} $mntpoint
log_must mounted $mntpoint

log_pass "zfs mount helper correctly handles both device and pool strings"
