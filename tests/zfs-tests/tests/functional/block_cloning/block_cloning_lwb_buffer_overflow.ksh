#!/bin/ksh -p
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
# Copyright (c) 2023 by iXsystems, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Test for LWB buffer overflow with multiple VDEVs ZIL when 128KB
#	block write is split into two 68KB ones, trying to write maximum
#	sizes 128KB TX_CLONE_RANGE record with 1022 block pointers into
#	68KB buffer.
#
# STRATEGY:
#	1. Create a pool with multiple VDEVs ZIL
#	2. Write maximum sizes TX_CLONE_RANGE record with 1022 block
#	   pointers into 68KB buffer
#	3. Sync TXG
#	4. Clone the file
#	5. Synchronize cached writes
#

verify_runnable "global"

if is_linux && [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a $VDIR/b $VDIR/c"
LDEV="$VDIR/e $VDIR/f"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Test for LWB buffer overflow with multiple VDEVs ZIL"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV $LDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV \
	log mirror $LDEV
log_must zfs create -o recordsize=32K $TESTPOOL/$TESTFS
# Each ZIL log entry can fit 130816 bytes for a block cloning operation,
# so it can store 1022 block pointers. When LWB optimization is enabled,
# an assert is hit when 128KB block write is split into two 68KB ones
# for 2 SLOG devices
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file1 bs=32K count=1022 \
	conv=fsync
sync_pool $TESTPOOL
log_must clonefile -f /$TESTPOOL/$TESTFS/file1 /$TESTPOOL/$TESTFS/file2
log_must sync

sync_pool $TESTPOOL
log_must have_same_content /$TESTPOOL/$TESTFS/file1 /$TESTPOOL/$TESTFS/file2
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS file1 $TESTPOOL/$TESTFS file2)
# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
log_must [ "$blocks" = "$(seq -s " " 0 1021 | sed 's/ $//')" ]

log_pass "LWB buffer overflow is not triggered with multiple VDEVs ZIL"

