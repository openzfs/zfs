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
# Copyright (c) 2023, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib
. $STF_SUITE/tests/functional/bclone/bclone_common.kshlib

verify_runnable "global"

verify_crossfs_block_cloning

claim="The copy_file_range syscall can clone across datasets."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS2

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS1/file1 bs=128K count=4
log_must sync_pool $TESTPOOL

log_must \
    clonefile -f /$TESTPOOL/$TESTFS1/file1 /$TESTPOOL/$TESTFS2/file2 0 0 524288
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/$TESTFS1/file1 /$TESTPOOL/$TESTFS2/file2

typeset blocks=$(get_same_blocks \
  $TESTPOOL/$TESTFS1 file1 $TESTPOOL/$TESTFS2 file2)
log_must [ "$blocks" = "0 1 2 3" ]

log_pass $claim
