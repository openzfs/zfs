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
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

if [[ $(linux_version) -lt $(linux_version "5.3") ]]; then
  log_unsupported "copy_file_range can't copy cross-filesystem before Linux 5.3"
fi

claim="The copy_file_range syscall can clone across datasets."

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS2

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS1/file1 bs=128K count=4
log_must sync_pool $TESTPOOL

log_must \
    clonefile -f /$TESTPOOL/$TESTFS1/file1 /$TESTPOOL/$TESTFS2/file2 0 0 524288
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/$TESTFS1/file1 /$TESTPOOL/$TESTFS2/file2

typeset blocks=$(get_same_blocks \
  $TESTPOOL/$TESTFS1 file1 $TESTPOOL/$TESTFS2 file2)
log_must [ "$blocks" = "0 1 2 3" ]

log_pass $claim
