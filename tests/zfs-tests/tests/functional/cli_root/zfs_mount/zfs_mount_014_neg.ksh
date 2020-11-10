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
# Verify zfs mount helper failure on known bad parameters
#

verify_runnable "both"

set -A vdevs $(get_disklist_fullpath $TESTPOOL)
vdev=${vdevs[0]}

mntpoint="$(get_prop mountpoint $TESTPOOL)"
helper="mount.zfs -o zfsutil"
fs=$TESTPOOL/$TESTFS

function cleanup
{
	log_must force_unmount $vdev
	return 0
}
log_onexit cleanup

log_note "Verify zfs mount helper failure on known bad parameters"

# Ensure that the ZFS filesystem is unmounted.
force_unmount $fs

log_note "Verify failure without '-o zfsutil'"
log_mustnot mount.zfs $fs $mntpoint

log_note "Verify '-o abc <device> <path>' bad option fails"
log_mustnot ${helper},abc $vdev $mntpoint

log_note "Verify '\$NONEXISTFSNAME <path>' fails"
log_mustnot $helper $NONEXISTFSNAME $mntpoint

log_note "Verify '<dataset> (\$NONEXISTFSNAME|/dev/null)' fails"
log_mustnot $helper $fs $NONEXISTFSNAME
log_mustnot $helper $fs /dev/null

log_note "Verify '/dev/null <path>' fails"
log_mustnot $helper /dev/null $mntpoint

log_note "Verify '[device|pool]' fails"
log_mustnot mount.zfs
log_mustnot $helper
log_mustnot $helper $vdev
log_mustnot $helper $TESTPOOL

log_pass "zfs mount helper fails when expected"