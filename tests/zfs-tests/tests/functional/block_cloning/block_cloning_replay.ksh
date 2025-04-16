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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Verify slogs are replayed correctly for cloned files. This
#	test is ported from slog_replay tests for block cloning.
#
# STRATEGY:
#	1. Create an empty file system (TESTFS)
#	2. Create regular files and sync
#	3. Freeze TESTFS
#	4. Clone the file
#	5. Unmount filesystem
#	   <At this stage TESTFS is frozen, the intent log contains a
#	   complete set of deltas to replay it>
#	6. Remount TESTFS <which replays the intent log>
#	7. Compare clone file with the original file
#

verify_runnable "global"

if is_linux && [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

export VDIR=$TEST_BASE_DIR/disk-bclone
export VDEV="$VDIR/a $VDIR/b $VDIR/c"
export LDEV="$VDIR/e $VDIR/f"
log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV $LDEV

claim="The slogs are replayed correctly for cloned files."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $TESTDIR $VDIR $VDIR2
}

log_onexit cleanup

#
# 1. Create an empty file system (TESTFS)
#
log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV \
	log mirror $LDEV
log_must zfs create $TESTPOOL/$TESTFS

#
# 2. TX_WRITE: Create two files and sync txg
#
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file1 \
    oflag=sync bs=128k count=4
log_must zfs set recordsize=16K $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file2 \
    oflag=sync bs=16K count=2048
sync_pool $TESTPOOL

#
# 3. Checkpoint for ZIL Replay
#
log_must zpool freeze $TESTPOOL

#
# 4. TX_CLONE_RANGE: Clone the file
#
log_must clonefile -f /$TESTPOOL/$TESTFS/file1 /$TESTPOOL/$TESTFS/clone1
log_must clonefile -f /$TESTPOOL/$TESTFS/file2 /$TESTPOOL/$TESTFS/clone2

#
# 5. Unmount filesystem and export the pool
#
# At this stage TESTFS is frozen, the intent log contains a complete set
# of deltas to replay for clone files.
#
log_must zfs unmount /$TESTPOOL/$TESTFS

log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS

log_must zpool export $TESTPOOL

#
# 6. Remount TESTFS <which replays the intent log>
#
# Import the pool to unfreeze it and claim log blocks.  It has to be
# `zpool import -f` because we can't write a frozen pool's labels!
#
log_must zpool import -f -d $VDIR $TESTPOOL

#
# 7. Compare clone file with the original file
#
log_must have_same_content /$TESTPOOL/$TESTFS/file1 /$TESTPOOL/$TESTFS/clone1
log_must have_same_content /$TESTPOOL/$TESTFS/file2 /$TESTPOOL/$TESTFS/clone2

typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS file1 \
	$TESTPOOL/$TESTFS clone1)
log_must [ "$blocks" = "0 1 2 3" ]

typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS file2 \
	$TESTPOOL/$TESTFS clone2)
# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
log_must [ "$blocks" = "$(seq -s " " 0 2047 | sed 's/ $//')" ]

log_pass $claim
