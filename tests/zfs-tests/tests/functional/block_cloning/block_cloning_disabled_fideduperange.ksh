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

claim="The FIDEDUPERANGE ioctl fails when block cloning is disabled."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=disabled $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=4
log_must sync_pool $TESTPOOL

log_mustnot clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 524288

log_pass $claim
