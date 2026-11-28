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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#
#
# DESCRIPTION:
#	FS_IOC_FSSETXATTR that fails on another attribute leaves the extent
#	size alone.
#
# STRATEGY:
#	1. Fill a project past its quota and try to move an empty file into
#	   it while pinning the file.  Check that it fails with EDQUOT and
#	   the file isn't pinned.
#	2. Try the same on a dataset without project quota (ENOTSUP), for a
#	   file and a directory.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "global"

typeset nopq=nopq_$$
typeset nopqvdev=$TEST_BASE_DIR/nopq.vdev
typeset -i prj=1011

function cleanup
{
	poolexists $nopq && destroy_pool $nopq
	rm -f $nopqvdev
	rmdir $TEST_BASE_DIR/nopq 2>/dev/null
	rm -rf $TESTDIR/prjfill $TESTDIR/empty
	zfs set projectquota@$prj=none $TESTPOOL/$TESTFS
}

log_onexit cleanup
log_assert "a refused FS_IOC_FSSETXATTR leaves the extent size alone"

# Over quota: zfs_setattr() refuses the new project ID.
log_must touch $TESTDIR/prjfill $TESTDIR/empty
log_must zfs project -p $prj $TESTDIR/prjfill
log_must dd if=/dev/urandom of=$TESTDIR/prjfill bs=128k count=8 2>/dev/null
log_must zpool sync $TESTPOOL
log_must zfs set projectquota@$prj=64k $TESTPOOL/$TESTFS
extsize_fails EDQUOT -p $prj -s 8192 $TESTDIR/empty
verify_ext $TESTDIR/empty none 0
log_must extsize -s 8192 $TESTDIR/empty
verify_ext $TESTDIR/empty extsize 8192

# Without project quota, FS_IOC_FSSETXATTR fails with ENOTSUP.
log_must truncate -s $MINVDEVSIZE $nopqvdev
log_must zpool create -o feature@project_quota=disabled -O mountpoint=none \
    $nopq $nopqvdev
log_must zfs create -o mountpoint=$TEST_BASE_DIR/nopq $nopq/fs
typeset mnt=$TEST_BASE_DIR/nopq
log_must touch $mnt/file
log_must mkdir $mnt/dir
for f in $mnt/file $mnt/dir; do
	extsize_fails EOPNOTSUPP -s 8192 $f
	verify_ext $f none 0
done

log_pass "a refused FS_IOC_FSSETXATTR leaves the extent size alone"
