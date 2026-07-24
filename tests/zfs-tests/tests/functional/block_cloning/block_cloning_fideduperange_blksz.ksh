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

claim="The FIDEDUPERANGE ioctl rejects files with differing block sizes."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

# Two files with identical content but different block sizes.  The clone step
# can only replace whole destination blocks with whole source blocks, so such a
# pair can never be deduped however equal its bytes are.
log_must zfs set recordsize=128K $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must zfs set recordsize=64K $TESTPOOL
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=64K count=8
log_must sync_pool $TESTPOOL

# Confirm the setup really produced different block sizes, otherwise the test
# would pass for the wrong reason.
typeset inblksz=$(stat_blksz /$TESTPOOL/file1)
typeset outblksz=$(stat_blksz /$TESTPOOL/file2)
log_must [ "$inblksz" = "131072" ]
log_must [ "$outblksz" = "65536" ]

typeset digest=$(xxh128digest /$TESTPOOL/file2)

# The request must be refused outright.  It must not be reported as merely
# differing: the bytes are identical, it is the layout that cannot be deduped.
# Assign without typeset so $? is the exit status of clonefile itself.
typeset out
typeset ret
out=$(clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 524288 2>&1)
ret=$?
log_note "clonefile exited $ret saying: $out"
log_must [ $ret -ne 0 ]
echo "$out" | grep -q "Invalid argument" || \
    log_fail "expected EINVAL, got: $out"
echo "$out" | grep -q "range differs" && \
    log_fail "reported DIFFERS for identical bytes: $out"

log_must sync_pool $TESTPOOL

log_must [ "$digest" = "$(xxh128digest /$TESTPOOL/file2)" ]
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ -z "$blocks" ]

log_pass $claim
