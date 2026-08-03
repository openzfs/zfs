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

claim="FIDEDUPERANGE compares the grown tail of a truncated file correctly."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled -O recordsize=128k $TESTPOOL $DISKS

# Two files written small and then truncated well out.  The truncate grows the
# block, so almost all of what gets compared is tail that was never written by
# anyone: the file's own bytes end at 1000 and the rest of the block reads back
# as zeros.  Both files are built the same way, so they must compare equal.
log_must file_write -o create -f /$TESTPOOL/file1 -b 1000 -c 1 -d 0
log_must file_write -o create -f /$TESTPOOL/file2 -b 1000 -c 1 -d 0
log_must truncate -s 65536 /$TESTPOOL/file1
log_must truncate -s 65536 /$TESTPOOL/file2
log_must sync_pool $TESTPOOL

typeset blksz=$(stat_blksz /$TESTPOOL/file1)
log_note "block size: $blksz, size: $(stat_size /$TESTPOOL/file1)"

# Both tails are zeros, so the two files are identical and must dedupe.
log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2
log_must clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 $blksz
log_must sync_pool $TESTPOOL
log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2

# Now make the tails differ, and the same request must report DIFFERS.
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=1 count=16 seek=2000 \
    conv=notrunc
log_must sync_pool $TESTPOOL

typeset digest=$(xxh128digest /$TESTPOOL/file2)
log_mustnot clonefile -d /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 $blksz
log_must sync_pool $TESTPOOL
log_must [ "$digest" = "$(xxh128digest /$TESTPOOL/file2)" ]

log_pass $claim
