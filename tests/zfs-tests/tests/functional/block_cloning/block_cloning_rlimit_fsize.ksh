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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	When block cloning is used to implement copy_file_range(2), the
#	RLIMIT_FSIZE limit must be respected.
#
# STRATEGY:
#	1. Create a pool.
#	2. ???
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Test for RLIMIT_FSIZE handling with block cloning enabled"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1 count=1000

ulimit -f 2
log_must clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 all
ulimit -f 1
log_mustnot clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file3 0 0 all

log_pass "copy_file_range(2) respects RLIMIT_FSIZE"
