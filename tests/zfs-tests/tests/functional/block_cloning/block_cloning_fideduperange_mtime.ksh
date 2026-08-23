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
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

claim="FIDEDUPERANGE leaves the destination's mtime and ctime unchanged."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=4
log_must sync_pool $TESTPOOL

typeset mtime0=$(stat_mtime /$TESTPOOL/file2)
typeset ctime0=$(stat_ctime /$TESTPOOL/file2)

# Sleep so that, were the timestamps updated, they would move to a later
# second and the comparison below would notice.
sleep 2

# Unlike a copy, a dedupe does not change the file content, so per the Linux
# REMAP_FILE_DEDUP convention it must not update mtime/ctime.
log_must clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 524288
log_must sync_pool $TESTPOOL

typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "0 1 2 3" ]

typeset mtime1=$(stat_mtime /$TESTPOOL/file2)
typeset ctime1=$(stat_ctime /$TESTPOOL/file2)
log_must [ "$mtime0" = "$mtime1" ]
log_must [ "$ctime0" = "$ctime1" ]

log_pass $claim
