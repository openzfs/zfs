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
#	Verify that cloning a file at a large offset is possible.
#
# STRATEGY:
#   1. Create dataset.
#   2. Populate the source file with 1024 blocks at 1024 block offset.
#   3. Clone 1024 blocks at a 1024-block offset.
#   4. Compare the cloned file with the original file.
#

verify_runnable "global"

claim="The first clone at a large offset is functional"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

#
# 1. Create dataset.
#
log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS
sync_pool $TESTPOOL

#
# 2. Populate the source file with 1024 blocks at 1024 block offset.
#
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 \
    oflag=sync bs=128k count=1024 seek=1024
sync_pool $TESTPOOL

#
# 3. Clone 1024 blocks at a 1024-block offset.
#
log_must clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file2 134217728 134217728 \
    134217728
sync_pool $TESTPOOL

#
# 4. Compare the cloned file with the original file.
#
log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)

# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
log_must [ "$blocks" = "$(seq -s " " 0 1023 | sed 's/ $//')" ]

log_pass $claim
