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

claim="FIDEDUPERANGE dedupes identical data stored with different compression."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k $TESTPOOL $DISKS

# The same bytes written either side of a compression property change: file1
# is stored verbatim, file2 compressed.  Their block pointers therefore differ
# in compression, psize and checksum, while the data they hold is identical.
# A block-pointer comparison must not conclude anything from that; only
# reading the data can settle it, and it must settle it as equal.
log_must zfs set compression=off $TESTPOOL
log_must file_write -o create -f /$TESTPOOL/file1 -b 131072 -c 4 -d 1
log_must zfs set compression=lz4 $TESTPOOL
log_must file_write -o create -f /$TESTPOOL/file2 -b 131072 -c 4 -d 1
log_must sync_pool $TESTPOOL

# Confirm the setup: file2 must really be stored smaller, or the two files
# would have identical block pointers and the test would prove nothing.
typeset b1=$(stat_blocks /$TESTPOOL/file1)
typeset b2=$(stat_blocks /$TESTPOOL/file2)
log_note "allocated blocks: file1=$b1 file2=$b2"
log_must [ "$b2" -lt "$b1" ]

log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2
log_must [ -z "$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)" ]

# Identical data must dedupe, however differently it happens to be stored.
log_must clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 524288
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "0 1 2 3" ]

log_pass $claim
