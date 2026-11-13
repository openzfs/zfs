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
# Copyright (c) 2026, Kyle Evans <kevans@FreeBSD.org>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	copy_file_range(2) cannot clone blocks over existing data in an
#	append-only file.
#
# STRATEGY:
#	1. Create source and destination files and make the destination
#	   append-only
#	2. Open the destination with O_APPEND, clear O_APPEND with F_SETFL,
#	   and try to clone the source over the start of the destination
#	3. Verify that the clone fails with EPERM and that the destination is
#	   unchanged
#

verify_runnable "global"

claim="copy_file_range cannot clone over data in an append-only file."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must dd if=/dev/urandom of=/$TESTPOOL/file2 bs=128K count=4
log_must sync_pool $TESTPOOL

log_must chflags sappnd /$TESTPOOL/file2
typeset hash=$(xxh128digest /$TESTPOOL/file2)

log_mustnot_expect "Operation not permitted" \
    append_only_ops clone /$TESTPOOL/file1 /$TESTPOOL/file2 0 524288
log_must sync_pool $TESTPOOL

log_must [ "$(xxh128digest /$TESTPOOL/file2)" = "$hash" ]
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ -z "$blocks" ]

log_pass $claim
