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
# Copyright (c) 2023, Klara Inc.
# Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

claim="copy_file_range will fall back to copy when cloning not possible."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=128K count=4
log_must sync_pool $TESTPOOL


log_note "Copying entire file with copy_file_range"

log_must clonefile -f /$TESTPOOL/file /$TESTPOOL/clone 0 0 524288
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file /$TESTPOOL/clone

typeset blocks=$(get_same_blocks $TESTPOOL file $TESTPOOL clone)
log_must [ "$blocks" = "0 1 2 3" ]


log_note "Copying within a block with copy_file_range"

log_must clonefile -f /$TESTPOOL/file /$TESTPOOL/clone 32768 32768 65536
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file /$TESTPOOL/clone

typeset blocks=$(get_same_blocks $TESTPOOL file $TESTPOOL clone)
log_must [ "$blocks" = "1 2 3" ]


log_note "Copying across a block with copy_file_range"

log_must clonefile -f /$TESTPOOL/file /$TESTPOOL/clone 327680 327680 131072
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file /$TESTPOOL/clone

typeset blocks=$(get_same_blocks $TESTPOOL file $TESTPOOL clone)
log_must [ "$blocks" = "1" ]

log_pass $claim
