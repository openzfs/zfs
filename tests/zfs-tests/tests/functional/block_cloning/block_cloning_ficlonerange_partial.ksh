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
# Copyright (c) 2023, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

claim="The FICLONERANGE ioctl can clone parts of a file."

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

log_must clonefile -r /$TESTPOOL/file1 /$TESTPOOL/file2 131072 131072 262144
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2

typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)
log_must [ "$blocks" = "1 2" ]

log_pass $claim
