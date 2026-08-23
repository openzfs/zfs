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

claim="FIDEDUPERANGE compares the whole range, so a differing sub-block reports DIFFERS."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k $TESTPOOL $DISKS

# Two single-block (128K) files that differ only in their first 4K.  A dedupe
# request shorter than the record size is rounded down to nothing by block
# alignment; the ioctl must still compare the requested bytes and report the
# ranges as differing rather than silently claiming they are the same.
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=1
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=1
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=4K count=1 conv=notrunc
log_must sync_pool $TESTPOOL

typeset digest=$(xxh128digest /$TESTPOOL/file2)

# The first 4K differ, so the ioctl must report DIFFERS specifically, not
# just any failure, and share nothing.
# Assign without typeset so $? is the exit status of clonefile itself.
typeset out
typeset ret
out=$(clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 4096 2>&1)
ret=$?
log_note "clonefile exited $ret saying: $out"
log_must [ $ret -ne 0 ]
echo "$out" | grep -q "range differs" || \
    log_fail "expected DIFFERS, got: $out"
log_must sync_pool $TESTPOOL

log_must [ "$digest" = "$(xxh128digest /$TESTPOOL/file2)" ]
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ -z "$blocks" ]

log_pass $claim
