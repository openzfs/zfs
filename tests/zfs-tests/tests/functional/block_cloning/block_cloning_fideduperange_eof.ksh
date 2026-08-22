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

claim="The FIDEDUPERANGE ioctl rejects a range running past the destination EOF."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k \
    $TESTPOOL $DISKS

# file2 holds the first three blocks of file1, so everything the request below
# actually covers is identical.  That is what makes this test meaningful: were
# the range trimmed to the destination EOF instead of refused, the trimmed part
# would compare equal and the dedupe would report success.
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=3
log_must sync_pool $TESTPOOL

log_must [ "$(stat_size /$TESTPOOL/file1)" = "524288" ]
log_must [ "$(stat_size /$TESTPOOL/file2)" = "393216" ]

typeset digest=$(xxh128digest /$TESTPOOL/file2)

# The source range ends exactly at file1's EOF, while the destination range
# runs one block past file2's.  A shortened dedupe cannot be reported as such
# (the ioctl hands back the length it was asked for), so the request has to
# fail rather than share what happens to fit.
# Assign without typeset so $? is the exit status of clonefile itself.
typeset out
typeset ret
out=$(clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 262144 262144 262144 2>&1)
ret=$?
log_note "clonefile exited $ret saying: $out"
log_must [ $ret -ne 0 ]
echo "$out" | grep -q "Invalid argument" || \
    log_fail "expected EINVAL, got: $out"
echo "$out" | grep -q "range differs" && \
    log_fail "reported DIFFERS for identical bytes: $out"

log_must sync_pool $TESTPOOL

log_must [ "$digest" = "$(xxh128digest /$TESTPOOL/file2)" ]
log_must [ -z "$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)" ]

log_pass $claim
