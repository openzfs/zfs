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

#
# DESCRIPTION:
#	A FIDEDUPERANGE is not logged to the ZIL, so a crash before the txg
#	syncs loses only the block sharing.  In particular replaying the log
#	must not restamp the deduped file's timestamps.
#
# STRATEGY:
#	1. Create a file system with a slog and two identical files
#	2. Freeze the pool
#	3. Dedupe one file against the other
#	4. Unmount and export, then import to replay the intent log
#	5. The content and the timestamps must be exactly as they were
#

verify_runnable "global"

export VDIR=$TEST_BASE_DIR/disk-fidedupe
export VDEV="$VDIR/a $VDIR/b $VDIR/c"
export LDEV="$VDIR/e $VDIR/f"
log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV $LDEV

claim="A deduped file's timestamps survive an intent log replay."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $TESTDIR $VDIR
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV \
    log mirror $LDEV
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file1 \
    oflag=sync bs=128K count=4
log_must dd if=/$TESTPOOL/$TESTFS/file1 of=/$TESTPOOL/$TESTFS/file2 \
    oflag=sync bs=128K count=4
sync_pool $TESTPOOL

typeset digest=$(xxh128digest /$TESTPOOL/$TESTFS/file2)
typeset mtime=$(stat_mtime /$TESTPOOL/$TESTFS/file2)
typeset ctime=$(stat_ctime /$TESTPOOL/$TESTFS/file2)

# Checkpoint for ZIL replay.
log_must zpool freeze $TESTPOOL

log_must clonefile -d /$TESTPOOL/$TESTFS/file1 /$TESTPOOL/$TESTFS/file2 \
    0 0 524288

log_must zfs unmount /$TESTPOOL/$TESTFS

# The dedupe must have left nothing behind to replay.
log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS
zdb -iv $TESTPOOL/$TESTFS 2>&1 | grep -q "TX_CLONE_RANGE" && \
    log_fail "dedupe logged a TX_CLONE_RANGE record"

log_must zpool export $TESTPOOL

# Import to unfreeze and replay the log.  It has to be `zpool import -f`
# because we can't write a frozen pool's labels.
log_must zpool import -f -d $VDIR $TESTPOOL

# Nothing the dedupe did may show through: same bytes, same times.
log_must [ "$digest" = "$(xxh128digest /$TESTPOOL/$TESTFS/file2)" ]
log_must [ "$mtime" = "$(stat_mtime /$TESTPOOL/$TESTFS/file2)" ]
log_must [ "$ctime" = "$(stat_ctime /$TESTPOOL/$TESTFS/file2)" ]

log_pass $claim
