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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	FICLONERANGE can clone the partial last block of a file to an
#	offset past the end of the destination file.
#
# STRATEGY:
#	1. Create a file of two full blocks and a 1000-byte tail.
#	2. Clone the tail to offset 1 MiB of a new, empty file.
#	3. Check the new file's size, the cloned bytes, and that the
#	   first 1 MiB reads as zeros.
#

verify_runnable "global"

claim="FICLONERANGE clones a partial tail past the destination EOF"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $TEST_BASE_DIR/tail.want $TEST_BASE_DIR/tail.got
}

log_onexit cleanup

typeset -li bs=131072
typeset -li tail=1000
typeset -li dst=1048576
typeset fs=/$TESTPOOL

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL \
    $DISKS
log_must eval "head -c $((2 * bs + tail)) /dev/urandom > $fs/file1"
sync_pool $TESTPOOL

log_must clonefile -r $fs/file1 $fs/file2 $((2 * bs)) $dst $tail
sync_pool $TESTPOOL

log_must [ $(stat_size $fs/file2) -eq $((dst + tail)) ]
log_must eval "tail -c $tail $fs/file1 > $TEST_BASE_DIR/tail.want"
log_must eval "tail -c $tail $fs/file2 > $TEST_BASE_DIR/tail.got"
log_must cmp $TEST_BASE_DIR/tail.want $TEST_BASE_DIR/tail.got
log_must cmp -n $dst $fs/file2 /dev/zero

log_pass $claim
