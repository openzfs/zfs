#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

claim="FIDEDUPERANGE compares only the bytes the range covers in a trailing block."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k $TESTPOOL $DISKS

# Two 256K (2 x 128K block) files that agree over the first 132K and differ
# from 132K on.  A 132K dedupe request covers all of block 0 and only the
# first 4K of block 1, so the bytes it covers are identical even though the
# two block 1s are not: it must report the ranges as the same.  Comparing any
# byte past the requested 132K would wrongly report DIFFERS.
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=2
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=2
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=4K count=31 seek=33 \
    conv=notrunc
log_must sync_pool $TESTPOOL

log_must [ -z "$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)" ]

# The covered bytes match, so the dedupe succeeds.  Only whole blocks can be
# shared, so the request is aligned down to block 0; block 1 keeps its own
# (differing) content.
log_must clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 135168
log_must sync_pool $TESTPOOL

typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "0" ]

# The dedupe must not have disturbed the part of file2 outside the range.
log_must cmp -n 135168 /$TESTPOOL/file1 /$TESTPOOL/file2
log_mustnot cmp -s /$TESTPOOL/file1 /$TESTPOOL/file2

log_pass $claim
