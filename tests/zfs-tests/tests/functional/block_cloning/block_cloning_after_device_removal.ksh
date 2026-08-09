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
#	Verify that when modifying and freeing cloned blocks after a top-level
#	vdev removal, there is no panic. This is a regression test for #17180.
#

verify_runnable "global"

export VDIR=$TEST_BASE_DIR/disk-bclone
export VDEV="$VDIR/0 $VDIR/1"
log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV

claim="No panic when destroying dataset with cloned blocks after top-level vdev removal"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $TESTDIR $VDIR
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=16M count=2
log_must zpool remove -w $TESTPOOL $VDIR/1
log_must zfs create $TESTPOOL/$TESTFS
log_must clonefile -f /$TESTPOOL/file /$TESTPOOL/$TESTFS/file
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=16M count=2
log_must zfs destroy -r $TESTPOOL/$TESTFS
wait_freeing $TESTPOOL
sync_pool $TESTPOOL

log_must zdb -b $TESTPOOL

log_pass $claim
