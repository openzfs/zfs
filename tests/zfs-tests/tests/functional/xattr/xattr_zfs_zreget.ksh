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
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# See ZoL issues #5866 and #8858.  This test will ensure the fixes for
# these specific issues have no regression.
#
# Exercise the functions zfs_suspend_fs / zfs_resume_fs / zfs_rezget.
# The test will ensure that we reach zfs code that compares file
# generation numbers and fetches extended attributes.
#
# STRATEGY:
# 1. Import a pool that has txg number > 2^32.
# 2. Take snapshot and do send with an online receive.
# 3. EIO is expected when accessing the received file-system.
# 4. Access extended attribute. (previously this would panic)
# 5. Take second snapshot, write some data.
# 6. Rollback.
# 7. Access the resulting file-system. (previously every accesss would give EIO)
#

verify_runnable "global"

function cleanup
{
	log_must zpool destroy -f txg-number-pool
	log_must rm $TEST_BASE_DIR/zfs-txg-number.dat
	check_mem_leaks
}


log_assert "zfs can handle suspend/resume with large generation number"

log_onexit cleanup

log_must bzcat \
    $STF_SUITE/tests/functional/xattr/blockfiles/zfs-txg-number.dat.bz2 \
    >$TEST_BASE_DIR/zfs-txg-number.dat

log_must zpool import txg-number-pool -d $TEST_BASE_DIR

log_must zfs create txg-number-pool/fs1
log_must zfs snapshot txg-number-pool/fs1@snap1
log_must zfs create txg-number-pool/fs2

log_must eval "zfs send txg-number-pool/fs1@snap1 |
    online_recv txg-number-pool/fs2@snap1"

log_mustnot stat /txg-number-pool/fs2
log_mustnot attr -l /txg-number-pool/fs2

log_must zfs snapshot txg-number-pool/fs1@snap2
log_must fill_fs /txg-number-pool/fs1 2 2 1024 1 R
log_must sync
log_must zfs rollback txg-number-pool/fs1@snap2

log_must stat /txg-number-pool/fs1
check_mem_leaks
log_pass "suspend/resume works with large generation number"
