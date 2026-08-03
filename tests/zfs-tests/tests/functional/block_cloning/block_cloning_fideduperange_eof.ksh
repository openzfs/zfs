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
