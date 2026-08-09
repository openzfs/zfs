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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

if is_freebsd && [[ $(freebsd_version) -lt $(freebsd_version "15.0") ]]; then
	log_unsupported "COPY_FILE_RANGE_CLONE not available before FreeBSD 15"
fi

claim="The copy_file_range(CLONE) syscall can clone parts of a file."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128K count=4
log_must sync_pool $TESTPOOL

log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128K count=4
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2

typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "" ]

log_must clonefile -F /$TESTPOOL/file1 /$TESTPOOL/file2 131072 131072 262144
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2

typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "1 2" ]

log_pass $claim
