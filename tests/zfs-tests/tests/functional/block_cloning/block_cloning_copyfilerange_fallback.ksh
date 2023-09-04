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
# Copyright (c) 2023, Klara Inc.
# Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

if [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

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
